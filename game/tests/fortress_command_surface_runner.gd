extends SceneTree
## Fortress command-surface gate (bugs A/B/C of the castle system).
##
## Retail oracle (PURE RotWK retail tree,
## workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##
##   * commandset.ini <Faction>FortressExpansionPad{Corner,Side}CommandSet gives
##     every fortress build plot that faction's own building menu — Angmar's
##     stone thrower / battle tower / kennel / wall hub (commandset.ini:3538),
##     Men's trebuchet / arrow tower / garrison dormitory / wall hub (:4094).
##   * commandset.ini <Faction>FortressCommandSet slots 8-14 are the fortress
##     improvement (OBJECT_UPGRADE) page — MordorFortressCommandSet DoomPyres /
##     LavaMoat / FireArrows / MagmaCauldrons / MorgulSorcery / GorgorothSpire
##     (:4655), AngmarFortressCommandSet Banners / Spikes / IceMunitions /
##     HouseOfLamentation / IceWalls / Sanctum (:3507). Each button buys a
##     *Trigger* upgrade that a `CastleUpgrade` behavior converts into the real
##     one (angmarfortress.ini:1282-1286).
##   * commandset.ini slots 15-24 are the hero revive page; the roster is
##     playertemplate.ini BuildableHeroesMP (FactionAngmar = Hwaldar, Karsh,
##     Morgramir, Rogash, Witchking).
##
## Sections
##   1. Fortress plots offer the authored building set (per faction).
##   2. Fortress hero roster is non-empty and matches the retail names.
##   3. The CastleUpgrade purchase path: a fortress improvement contract is
##      purchasable and its trigger hands out the real upgrade to the whole
##      castle. Driven by a fixture surface because no shipped pack carries the
##      castleUpgrades surface yet (see workspace/scratch/opus16-fortress-report.md).
##
## Run:
##   OPENBFME_CONTENT=<repo>/workspace/content-packs godot --headless --path game \
##     --script res://tests/fortress_command_surface_runner.gd
## One faction per process: OPENBFME_SLICE_FACTION selects it (default angmar).

const BOOT_DEADLINE_MS := 300000
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CahHeroesScript = preload("res://src/content/cah_heroes.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

## Retail expectations per faction, transcribed from the oracle cited above.
## `plots` are the runtime expansion kinds the pad command sets authorize;
## `heroes` are the BuildableHeroesMP roster entries (CreateAHero excluded — the
## create-a-hero system is its own lane).
const FACTION_EXPECTATIONS := {
	"angmar": {
		"plots": ["battletowerexpansion", "catapultexpansion", "kennelexpansion", "wall_hub_small_expansion"],
		"heroes": ["AngmarHwaldar", "AngmarKarsh", "AngmarMorgramir", "AngmarRogash", "AngmarWitchking"],
	},
	"men": {
		"plots": ["arrow_tower_expansion", "garrison_dormitory", "trebuchet_expansion", "trebuchet_side_expansion", "wall_hub_small_expansion"],
		"heroes": ["GondorAragornMP", "GondorBoromir", "GondorFaramir", "GondorGandalf", "RohanEomer", "RohanEowyn", "RohanTheoden"],
	},
	"wild": {
		"plots": ["arrowdenexpansion", "burrowsexpansion", "giantsentryexpansion", "spiderholesexpansion"],
		"heroes": ["WildAzog", "WildGoblinKing", "WildShelob"],
	},
	# Dwarves — commandset.ini:4169/:4176 DwarvenFortressExpansionPad{Corner,Side}
	# CommandSet (catapult / Erebor axe tower / hall / wall hub) and
	# playertemplate.ini:248 BuildableHeroesMP FactionDwarves
	# (CreateAHero excluded — the created hero rides section 5).
	"dwarves": {
		"plots": ["catapultexpansion", "erebortowertowerexpansion", "hallexpansion", "wall_hub_small_expansion"],
		"heroes": ["DwarvenCaptainofDale", "DwarvenDain", "DwarvenGimli", "DwarvenGloin"],
	},
}

## Retail's fortress radial is PAGED, not one flat arc: the main page carries
## slots 1-6, `Command_SelectUpgrades<Faction>Fortress`
## (Command = PUSH_VISIBLE_COMMAND_RANGE, CommandRangeStart 7 / Count 7 —
## commandbutton.ini:13566) opens slots 8-14, and
## `Command_SelectRevivables<Faction>Fortress` (CommandRangeStart 14 / Count 10,
## :13577) opens the hero slots 15-24. Both pages end on `Command_RadialBack`
## (Command = POP_VISIBLE_COMMAND_RANGE, :36).
const RADIAL_PAGE_MAIN := "main"
const RADIAL_PAGE_UPGRADES := "upgrades"
const RADIAL_PAGE_HEROES := "heroes"
const PAGE_SELECTOR_EXPECTATIONS := {
	"dwarves": {
		"upgrades": ["Command_SelectUpgradesDwarvenFortress", "CONTROLBAR:SelectUpgradesDwarvenFortress", 7, 7],
		"heroes": ["Command_SelectRevivablesDwarvenFortress", "CONTROLBAR:SelectRevivablesDwarvenFortress", 14, 10],
	},
	"angmar": {
		# Retail deliberately authors the Dwarven string ids on Angmar's buttons.
		"upgrades": ["Command_SelectUpgradesAngmarFortress", "CONTROLBAR:SelectUpgradesDwarvenFortress", 7, 7],
		"heroes": ["Command_SelectRevivablesAngmarFortress", "CONTROLBAR:SelectRevivablesDwarvenFortress", 14, 10],
	},
	"wild": {
		"upgrades": ["Command_SelectUpgradesWildFortress", "CONTROLBAR:SelectUpgradesWildFortress", 6, 7],
		"heroes": ["Command_SelectRevivablesWildFortress", "CONTROLBAR:SelectRevivablesGoblinFortress", 13, 10],
	},
}

## The nine images consumed by Mordor's two page selectors, six fortress
## improvements, and back button. These are retail's ButtonImage ids (including
## the authored Gorgonoth spelling), not runtime-invented aliases.
const MORDOR_FORTRESS_IMAGE_IDS: Array[String] = [
	"UCCommon_UpgradeStructureNew",
	"UCCommon_EvilHeroes",
	"BMFortress_DoomPyres",
	"BMFortress_LavaMoat",
	"BMOrcPit_FlamingArrows",
	"BMFortress_MagmaCauldrons",
	"BMFortress_MorgulSorcery",
	"BMFortress_GorgonothSpire",
	"UCCommon_BackArrow",
]

## Localized fortress-improvement labels, transcribed from the 2.01 string table
## (lang/englishpatch201.big -> data/lotr.str). The runtime must present THESE,
## not a humanized copy of the upgrade id.
## Keyed by faction because the SAME upgrade is sold under different retail text
## by different factions: Upgrade_GoodFortressFlamingMunitionsTrigger is
## "&Masterwork Munitions" on the Dwarven fortress (lotr.str:27148) and
## "&Flaming Munitions" on the Men fortress (:26879).
const RETAIL_UPGRADE_LABELS := {
	"dwarves": {
		"Upgrade_DwarvenFortressDwarvenStoneworkTrigger": "Dwarven S&tonework",
		"Upgrade_GoodFortressFlamingMunitionsTrigger": "&Masterwork Munitions",
	},
	"men": {
		"Upgrade_MenFortressNumenorStoneworkTrigger": "Numenor S&tonework",
		"Upgrade_GoodFortressFlamingMunitionsTrigger": "&Flaming Munitions",
	},
}
## gamedata.ini DWARVEN_STONEWORK_BANNER_BUILDCOST (:776),
## GONDOR_NEMENORSTONEWORK_BUILDCOST (:2439), upgrade.ini:1611.
const RETAIL_UPGRADE_COSTS := {
	"dwarves": {
		"Upgrade_DwarvenFortressDwarvenStoneworkTrigger": 2000,
		"Upgrade_GoodFortressFlamingMunitionsTrigger": 1500,
	},
	"men": {
		"Upgrade_MenFortressNumenorStoneworkTrigger": 2000,
		"Upgrade_GoodFortressFlamingMunitionsTrigger": 1500,
	},
}

var passed := 0
var failed := 0
var _faction := "angmar"
var _cah_seed_error := ""

## Silent-abort guard. A GDScript runtime error inside an `await`-ing function
## unwinds it WITHOUT failing anything, so a runner that only counts failures
## prints a confident green while whole sections never ran — this file's
## sections 8 and 9 are await-heavy and have legitimate early returns, so a
## raw check-count pin would be brittle across factions. Instead every section
## records that it finished, and _finish refuses a run that stopped early.
var _sections_completed: Array[String] = []
## Sections _run intends to reach; appended as it commits to each one, so a
## legitimate early exit (unknown faction, boot failure) does not demand
## sections it never started.
var _sections_expected: Array[String] = []


func _section_reached(name: String) -> void:
	if not _sections_expected.has(name):
		_sections_expected.append(name)


func _section_completed(name: String) -> void:
	if not _sections_completed.has(name):
		_sections_completed.append(name)


func _initialize() -> void:
	_runner_watchdog.start(self, "FORTRESS_COMMAND_SURFACE_RUNNER")
	_faction = OS.get_environment("OPENBFME_SLICE_FACTION").strip_edges().to_lower()
	if _faction == "":
		_faction = "angmar"
		OS.set_environment("OPENBFME_SLICE_FACTION", _faction)
	for env_name in ["OPENBFME_SLICE_MAP", "OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	# Section 6 SAVES a Create-a-Hero profile, so this runner owns a store of its
	# own, per faction. Sharing the headless scratch store would let two gate
	# processes clear each other's seed mid-run (and one day reach the installed
	# game's real heroes, which is the hazard cah_heroes.profile_dir() exists to
	# prevent).
	if OS.get_environment(CahHeroesScript.PROFILE_DIR_ENV).strip_edges() == "":
		OS.set_environment(
			CahHeroesScript.PROFILE_DIR_ENV, "user://cah-heroes-fortress-gate-%s" % _faction
		)
	call_deferred("_run")


func _run() -> void:
	_check_castle_upgrade_surface_shapes()
	_check_castle_upgrade_purchase_path()
	_check_mordor_fortress_interface_art()
	if not FACTION_EXPECTATIONS.has(_faction):
		print("RESULT fortress_surface faction=%s SKIPPED (no transcribed retail expectation)" % _faction)
		return _finish()
	var expectation: Dictionary = FACTION_EXPECTATIONS[_faction]

	# Seeded BEFORE the slice is built: the roster is assembled during boot, so a
	# hero saved afterwards would never reach the fortress.
	_cah_seed_error = _seed_created_hero()

	root.size = Vector2i(1920, 1080)
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null:
		_check("slice_scene_loads", false, "res://scenes/retail_vertical_slice.tscn did not load")
		return _finish()
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("%s_slice_boots" % _faction, bool(slice.ready_ok), String(slice.failure_reason)):
		return _finish()

	var sim = slice.simulation
	var team := 0
	var fortress := int(sim.fortress_id(team))
	if not _check("%s_fortress_exists" % _faction, fortress != 0, "team 0 seeded no fortress"):
		return _finish()

	# --- Section 1: plots offer the authored building set ---------------------
	var pads: Array = sim.expansion_pad_states(fortress)
	_check("%s_fortress_has_pads" % _faction, not pads.is_empty(), "fortress unpacked no expansion pads")
	var offered: Array = []
	for kind_value in sim.expansion_commands_for(fortress):
		offered.append(String(kind_value))
	offered.sort()
	var expected_plots: Array = (expectation["plots"] as Array).duplicate()
	expected_plots.sort()
	_check(
		"%s_plots_offer_authored_buildings" % _faction,
		offered == expected_plots,
		"expected %s, got %s" % [str(expected_plots), str(offered)]
	)
	# Every offered kind must actually be buildable on a pad the retail command
	# set authorizes — an offer with no matching free pad is a phantom button.
	for kind_value in offered:
		var rule: Dictionary = sim._expansion_build_rules.get(String(kind_value), {})
		var pad_kinds: Array = rule.get("pad_kinds", [])
		_check(
			"%s_plot_%s_binds_a_pad_kind" % [_faction, String(kind_value)],
			not pad_kinds.is_empty(),
			"expansion '%s' records no pad kinds" % String(kind_value)
		)

	# --- Section 2: hero roster ----------------------------------------------
	var production: Array = (sim.structure(fortress) as Dictionary).get("production", [])
	var hud = slice.hud
	# Retail identity, not runtime slug: the HUD hero specs carry the authored
	# source object id (AngmarKarsh), which is what the retail roster names.
	var hero_sources: Array = []
	var hero_unit_ids: Dictionary = {}
	for spec_value in (hud._hero_command_specs if hud != null else []):
		var spec: Dictionary = spec_value
		if String(spec.get("producer_kind", "")) != "fortress":
			continue
		if not production.has(String(spec.get("unit_id", ""))):
			continue
		if String(spec.get("source_object_id", "")).begins_with("CreateAHero__"):
			# The created hero is not a BuildableHeroesMP entry; section 6 owns it.
			continue
		hero_sources.append(String(spec.get("source_object_id", "")))
		hero_unit_ids[String(spec.get("unit_id", ""))] = true
	hero_sources.sort()
	var expected_heroes: Array = (expectation["heroes"] as Array).duplicate()
	expected_heroes.sort()
	_check(
		"%s_fortress_hero_roster_non_empty" % _faction,
		not hero_sources.is_empty(),
		"fortress offered no heroes at all"
	)
	_check(
		"%s_fortress_hero_roster_matches_retail" % _faction,
		hero_sources == expected_heroes,
		"expected %s, got %s" % [str(expected_heroes), str(hero_sources)]
	)
	# The HUD must carry a button for each of them, or the roster is invisible.
	var hero_buttons: Dictionary = hud.hero_buttons if hud != null else {}
	var bound := 0
	for unit_id_value in hero_unit_ids.keys():
		if hero_buttons.has(String(unit_id_value)):
			bound += 1
	_check(
		"%s_fortress_hero_roster_is_bound_in_the_hud" % _faction,
		bound == hero_sources.size(),
		"%d of %d fortress heroes have a HUD button" % [bound, hero_sources.size()]
	)

	# --- Sections 4-6: the retail PAGED radial --------------------------------
	# NOTE: completion is recorded INSIDE each section, never here. A runtime
	# error inside an awaited coroutine unwinds that coroutine and hands control
	# straight back to this function, so marking a section done after its
	# `await` returned would mark an aborted section as complete — which is
	# exactly the failure mode this guard exists to catch.
	_section_reached("radial_pages")
	await _check_fortress_radial_pages(slice, sim, hud, fortress)
	# --- Section 8: a fortress the PLAYER builds ------------------------------
	_section_reached("constructed_fortress_parity")
	await _check_constructed_fortress_parity(slice, sim, hud, fortress)
	# --- Section 9: hero portrait double-click centres the camera -------------
	_section_reached("hero_portrait_focus")
	_check_hero_portrait_double_click_centres_the_camera(slice, sim, hud)
	_finish()


func _check_hero_portrait_double_click_centres_the_camera(slice, sim, hud) -> void:
	## Owner playtest bug C: "When I double-click a hero from the hero menu it
	## needs to center the camera on them." Single click selects (retail), the
	## double click additionally jumps the view. Driven through the HUD's real
	## portrait input handler so the wiring — not a reimplementation — is gated.
	if hud == null or not hud.has_method("_on_hero_button_gui_input"):
		_check("%s_hero_bar_has_a_double_click_path" % _faction, false, "RetailHud has no portrait input handler")
		return
	# The bar is built from living team-0 entities whose category is "hero"
	# (_sync_hero_bar). If this fixture fields none, any living player entity
	# still exercises the same portrait -> camera wiring, so the gate degrades
	# rather than vanishing; the subject is reported either way.
	var hero_id := 0
	var subject := "hero"
	for id in sim.entity_ids():
		var row: Dictionary = sim.entity(id)
		if int(row.get("team", -1)) != 0 or int(row.get("health", 0)) <= 0:
			continue
		if String(row.get("category", "")) == "hero":
			hero_id = id
			subject = "hero %d" % id
			break
		if hero_id == 0:
			hero_id = id
			subject = "non-hero entity %d" % id
	if not _check(
		"%s_hero_bar_has_a_subject_to_double_click" % _faction,
		hero_id != 0,
		"team 0 fields no living entity at all"
	):
		return
	print("HERO_FOCUS subject=%s id=%d" % [subject, hero_id])

	# Park the camera somewhere the hero is NOT, so a no-op cannot pass.
	var hero_at := Vector2(sim.entity(hero_id).get("position", Vector2.ZERO))
	slice._center_camera_on(hero_at + Vector2(28.0, 22.0))
	var parked := Vector2(slice.camera_focus)
	if not _check(
		"%s_camera_can_be_parked_away_from_the_hero" % _faction,
		parked.distance_to(hero_at) > 1.0,
		"camera clamped onto the hero at %s, so the jump is unobservable" % str(parked)
	):
		return

	# A SINGLE click must select without moving the view.
	var single := InputEventMouseButton.new()
	single.button_index = MOUSE_BUTTON_LEFT
	single.pressed = true
	hud._on_hero_button_gui_input(single, hero_id)
	_check(
		"%s_hero_portrait_single_click_does_not_move_the_camera" % _faction,
		Vector2(slice.camera_focus).is_equal_approx(parked),
		"a single click moved the camera to %s" % str(slice.camera_focus)
	)

	# The DOUBLE click jumps to him.
	var double := InputEventMouseButton.new()
	double.button_index = MOUSE_BUTTON_LEFT
	double.pressed = true
	double.double_click = true
	hud._on_hero_button_gui_input(double, hero_id)
	var focus := Vector2(slice.camera_focus)
	_check(
		"%s_hero_portrait_double_click_centres_the_camera" % _faction,
		focus.distance_to(hero_at) < parked.distance_to(hero_at) and focus.distance_to(hero_at) <= 1.0,
		"hero at %s, camera went from %s to %s" % [str(hero_at), str(parked), str(focus)]
	)
	_check(
		"%s_hero_portrait_double_click_also_selects_him" % _faction,
		sim.selected_ids.has(hero_id),
		"selection is %s" % str(sim.selected_ids)
	)
	# The checks above drive the handler directly, so they would still pass if
	# the production wiring were deleted. Guard the wiring itself: the slice must
	# be listening, and a real portrait button must route its raw input here.
	_check(
		"%s_slice_listens_for_hero_focus" % _faction,
		hud.hero_focus_requested.get_connections().size() > 0,
		"nothing is connected to RetailHud.hero_focus_requested"
	)
	var wired_portraits := 0
	for button_value in hud._hero_bar_buttons.values():
		if (button_value as Button).gui_input.get_connections().size() > 0:
			wired_portraits += 1
	_check(
		"%s_hero_portraits_route_their_raw_input" % _faction,
		hud._hero_bar_buttons.is_empty() or wired_portraits == hud._hero_bar_buttons.size(),
		"%d of %d portrait buttons have a gui_input handler" % [wired_portraits, hud._hero_bar_buttons.size()]
	)
	_section_completed("hero_portrait_focus")


func _check_constructed_fortress_parity(slice, sim, hud, seed_fortress: int) -> void:
	## Owner playtest bug A: "The fortress I build doesn't have the menu for
	## building heroes/upgrades/builder next to the radar." A player-constructed
	## fortress came up with a COMPLETELY EMPTY palantir while the map-start
	## fortress showed its full wheel.
	##
	## The oracle is the map-start fortress of the SAME team and faction: two
	## fortresses one player owns must offer the same commands. Retail has no
	## notion of a second-class fortress — commandset.ini binds the command set
	## to the object, not to how the object came to exist.
	if not sim.structure_build_rules_for_team(0).has("fortress"):
		print("SKIP %s_constructed_fortress_parity (faction cannot build a fortress)" % _faction)
		_section_completed("constructed_fortress_parity")
		return
	var seed_surface := await _command_surface_snapshot(slice, hud, seed_fortress)

	slice._grant_test_resources()
	var builder_id := 0
	for id in sim.entity_ids():
		var row: Dictionary = sim.entity(id)
		if int(row.get("team", -1)) == 0 and int(row.get("health", 0)) > 0 and bool(row.get("is_builder", false)):
			builder_id = id
			break
	if not _check(
		"%s_a_builder_exists_to_build_a_second_fortress" % _faction,
		builder_id != 0,
		"team 0 fields no builder, so the construction path is untested"
	):
		return

	# A legal plot for a second fortress, probed with the sim's own dry run.
	var builder_ids: Array[int] = [builder_id]
	var builder_at := Vector2(sim.entity(builder_id).get("position", Vector2.ZERO))
	var site := Vector2.ZERO
	var has_site := false
	var refusal := "no candidate probed"
	for radius in [12.0, 18.0, 26.0, 34.0, 46.0]:
		for step in range(8):
			var angle := TAU * float(step) / 8.0
			var candidate := builder_at + Vector2(cos(angle), sin(angle)) * float(radius)
			var probe: Dictionary = sim.validate_construct_site(builder_ids, "fortress", candidate, 0)
			if bool(probe.get("ok", false)):
				site = candidate
				has_site = true
				break
			refusal = String(probe.get("reason", ""))
		if has_site:
			break
	if not _check(
		"%s_a_second_fortress_has_a_legal_site" % _faction, has_site, "last refusal: %s" % refusal
	):
		return

	var order: Dictionary = sim.issue_construct(builder_ids, "fortress", site, false, 0)
	if not _check(
		"%s_a_second_fortress_can_be_ordered" % _faction,
		bool(order.get("ok", false)),
		String(order.get("reason", ""))
	):
		return
	var built_id := int(order.get("structure_id", 0))

	# Walk the porter over and raise the building through the REAL step path, so
	# every authored completion side effect (pad seeding, create grants, events)
	# fires exactly as it does in a live match.
	var completed := false
	var hurried := false
	for _tick in range(6000):
		sim.tick()
		var site_row: Dictionary = sim.structure(built_id)
		if site_row.is_empty() or int(site_row.get("health", 0)) <= 0:
			break
		if float(site_row.get("construction_progress", 0.0)) >= 1.0:
			completed = true
			break
		if not hurried and String(sim.entity(builder_id).get("state", "")) == "construct":
			# The porter has arrived; skip the build timer, not the build path.
			sim.debug_finish_team_work(0)
			hurried = true
	if not _check(
		"%s_the_second_fortress_finishes_construction" % _faction,
		completed,
		"progress=%s health=%s builder_state=%s" % [
			str(sim.structure(built_id).get("construction_progress", -1.0)),
			str(sim.structure(built_id).get("health", -1)),
			String(sim.entity(builder_id).get("state", "")),
		]
	):
		return

	# The owner does not assign selected_structure_id — he CLICKS the building.
	# The left-click pick must land on the citadel, not on one of the castle
	# pieces CastleBehavior spawns around it: a piece produces nothing, so its
	# wheel is exactly the empty ring the playtest screenshot shows.
	_section_reached("left_click_map_start")
	_check_left_clicking_the_castle_selects_the_fortress(slice, sim, seed_fortress, "map_start")
	_section_reached("left_click_built")
	_check_left_clicking_the_castle_selects_the_fortress(slice, sim, built_id, "built")

	var built_surface := await _command_surface_snapshot(slice, hud, built_id)

	# The bug, stated as a gate: the built fortress's wheel is not empty.
	_check(
		"%s_built_fortress_main_page_is_not_empty" % _faction,
		not (built_surface[RADIAL_PAGE_MAIN] as Dictionary).get("ids", []).is_empty(),
		"the constructed fortress offers no commands at all (seed offers %s)" % str(
			(seed_surface[RADIAL_PAGE_MAIN] as Dictionary).get("ids", [])
		)
	)
	# ...and it is the SAME wheel the map-start fortress carries.
	for page_value in [RADIAL_PAGE_MAIN, RADIAL_PAGE_UPGRADES, RADIAL_PAGE_HEROES]:
		var page := String(page_value)
		var seed_page: Dictionary = seed_surface[page]
		var built_page: Dictionary = built_surface[page]
		_check(
			"%s_built_fortress_%s_page_matches_the_map_start_fortress" % [_faction, page],
			seed_page.get("ids", []) == built_page.get("ids", []),
			"seed=%s built=%s" % [str(seed_page.get("ids", [])), str(built_page.get("ids", []))]
		)
	# The sim-side surfaces the wheel is drawn from, checked directly so a HUD
	# failure and a sim failure are told apart.
	var seed_heroes: Array = Array(sim.structure(seed_fortress).get("production", [])).duplicate()
	var built_heroes: Array = Array(sim.structure(built_id).get("production", [])).duplicate()
	seed_heroes.sort()
	built_heroes.sort()
	_check(
		"%s_built_fortress_produces_the_same_roster" % _faction,
		seed_heroes == built_heroes,
		"seed=%s built=%s" % [str(seed_heroes), str(built_heroes)]
	)
	var seed_pads: Array = sim.expansion_commands_for(seed_fortress).duplicate()
	var built_pads: Array = sim.expansion_commands_for(built_id).duplicate()
	seed_pads.sort()
	built_pads.sort()
	_check(
		"%s_built_fortress_offers_the_same_expansions" % _faction,
		seed_pads == built_pads,
		"seed=%s built=%s" % [str(seed_pads), str(built_pads)]
	)
	var seed_upgrades: Array = _upgrade_ids(sim.structure_upgrade_commands(seed_fortress))
	var built_upgrades: Array = _upgrade_ids(sim.structure_upgrade_commands(built_id))
	_check(
		"%s_built_fortress_offers_the_same_improvements" % _faction,
		seed_upgrades == built_upgrades,
		"seed=%s built=%s" % [str(seed_upgrades), str(built_upgrades)]
	)
	# Stamping source_object_id is NOT snapshot-only. _structure_footprint_radius
	# falls through to _resolved_footprint_source_radius(source_object_id, kind),
	# so without an id a constructed building used the generic fallback radius
	# while the identical seeded building used its authored geometry — and that
	# radius feeds the attack-range gate against structures and unit eviction
	# around them. The fix makes the built fortress agree with the seeded one;
	# that INTENDED consequence is gated here rather than left implicit.
	# How much the identity is actually WORTH on this pack, measured rather than
	# assumed: the same row with source_object_id stripped is exactly the state
	# a constructed building used to be in.
	var fortress_stripped: Dictionary = (sim.structure(built_id) as Dictionary).duplicate(true)
	fortress_stripped.erase("source_object_id")
	fortress_stripped.erase("footprint_radius_source")
	print("FORTRESS_FOOTPRINT with=%.4f without=%.4f seed=%.4f" % [
		sim._structure_footprint_radius(sim.structure(built_id)),
		sim._structure_footprint_radius(fortress_stripped),
		sim._structure_footprint_radius(sim.structure(seed_fortress)),
	])
	_check(
		"%s_built_fortress_has_the_same_footprint_radius" % _faction,
		is_equal_approx(
			sim._structure_footprint_radius(sim.structure(built_id)),
			sim._structure_footprint_radius(sim.structure(seed_fortress))
		),
		"seed=%f built=%f" % [
			sim._structure_footprint_radius(sim.structure(seed_fortress)),
			sim._structure_footprint_radius(sim.structure(built_id)),
		]
	)
	_check(
		"%s_built_fortress_carries_its_retail_object_identity" % _faction,
		String(sim.structure(built_id).get("source_object_id", ""))
			== String(sim.structure(seed_fortress).get("source_object_id", "")),
		"seed=%s built=%s" % [
			String(sim.structure(seed_fortress).get("source_object_id", "")),
			String(sim.structure(built_id).get("source_object_id", "")),
		]
	)
	_section_reached("constructed_non_fortress_parity")
	_check_constructed_non_fortress_parity(slice, sim)
	_section_completed("constructed_fortress_parity")


func _check_left_clicking_the_castle_selects_the_fortress(slice, sim, fortress_id: int, label: String) -> void:
	## Clicking anywhere on a castle must leave the player holding the FORTRESS
	## and its command wheel — whichever branch of _handle_left_click gets there
	## (expansion pad, structure pick, or castle-piece resolution). Reported per
	## fortress so a general pick bug and a construction-only one are told apart.
	## Probes are scaled to the fortress's OWN footprint, so this asserts what a
	## player actually does — click on the building — rather than an invented
	## world-unit distance that may fall outside the silhouette entirely.
	var centre := Vector2(sim.structure(fortress_id).get("position", Vector2.ZERO))
	var radius := float(slice._structure_pick_radius(fortress_id, "fortress"))
	var mispicks: Array[String] = []
	var offsets: Array[Vector2] = [Vector2.ZERO]
	for fraction in [0.45, 0.85]:
		for step in range(8):
			var angle := TAU * float(step) / 8.0
			offsets.append(Vector2(cos(angle), sin(angle)) * radius * float(fraction))
	# The castle is far wider than the citadel's own footprint: CastleBehavior
	# unpacks wall and tower pieces that stand well outside `radius`, and they
	# are the parts of the silhouette a player most often clicks. Clicking one
	# must select the castle, not hand back a piece with an empty command set.
	for piece_value in Array(sim.structure(fortress_id).get("castle_piece_structure_ids", [])):
		var piece_row: Dictionary = sim.structure(int(piece_value))
		if piece_row.is_empty() or int(piece_row.get("health", 0)) <= 0:
			continue
		offsets.append(Vector2(piece_row.get("position", centre)) - centre)
	# Drive the REAL left-click handler, not a re-composition of the two helpers
	# it happens to call. _handle_left_click runs an expansion-pad test and a
	# battalion test FIRST, and on Men most of these probes are in fact
	# pad-intercepted — a check that skipped those branches was testing a path
	# the player never takes. (Pad interception is a correct outcome here: it
	# also selects the fortress, so the player still gets the castle's wheel.)
	var pad_intercepted := 0
	var battalion_intercepted := 0
	for probe in offsets:
		slice._selected_expansion_pad = {}
		sim.selected_ids.clear()
		slice.selected_structure_id = 0
		slice._handle_left_click(centre + probe, false)
		var picked := int(slice.selected_structure_id)
		if not slice._selected_expansion_pad.is_empty():
			pad_intercepted += 1
		if picked == fortress_id:
			continue
		if not sim.selected_ids.is_empty():
			# A friendly battalion standing on the castle won the click. That is
			# RETAIL behaviour, not a mispick: units are selected over the
			# buildings they stand on, and _handle_left_click tests the
			# battalion branch before the structure branch on purpose.
			battalion_intercepted += 1
			continue
		mispicks.append("offset %s -> id=%d kind=%s" % [
			str(probe), picked, String(sim.structure(picked).get("structure_kind", "<none>"))
		])
	# Leave no pad armed for the sections that follow.
	slice._selected_expansion_pad = {}
	sim.selected_ids.clear()
	# The probes above re-compose _closest_structure + _selection_target_structure,
	# so they would still pass if the left-click handler stopped calling the
	# resolver. Guard the call site itself (the precedent this project already
	# uses in attack_cursor_runner for the same reason).
	var click_source := ""
	var slice_script := FileAccess.open("res://src/retail_slice/retail_vertical_slice.gd", FileAccess.READ)
	if slice_script != null:
		click_source = slice_script.get_as_text()
		slice_script.close()
	_check(
		"%s_left_click_selection_routes_through_the_castle_resolver" % _faction,
		click_source.contains("_selection_target_structure(_closest_structure(point, local_team))"),
		"the left-click structure pick no longer resolves castle pieces to their fortress"
	)
	print("FORTRESS_PICK %s id=%d radius=%.3f pad=%d battalion=%d of %d pieces=%s" % [
		label, fortress_id, radius, pad_intercepted, battalion_intercepted, offsets.size(),
		str(sim.structure(fortress_id).get("castle_piece_structure_ids", []))
	])
	# The gate must not be satisfied by every probe being intercepted: at least
	# some clicks have to reach the structure branch this check exists to test.
	_check(
		"%s_%s_castle_probes_reach_the_structure_branch" % [_faction, label],
		pad_intercepted + battalion_intercepted + mispicks.size() < offsets.size(),
		"all %d probes were intercepted before the structure pick" % offsets.size()
	)
	_check(
		"%s_left_clicking_the_%s_castle_selects_the_fortress" % [_faction, label],
		mispicks.is_empty(),
		str(mispicks)
	)
	_section_completed("left_click_%s" % label)


func _check_constructed_non_fortress_parity(slice, sim) -> void:
	## The fortress is not a special case. `source_object_id` is what
	## _structure_footprint_radius resolves authored geometry through, and that
	## radius feeds the attack-range gate against structures and unit eviction -
	## so a barracks the map seeded and a barracks a porter raised must resolve
	## the SAME identity, or the same building fights and pushes units
	## differently depending on how it came to exist.
	##
	## The slice maps seed only a fortress (plus its castle pieces), so there is
	## usually no seeded barracks to compare against directly. The symmetry is
	## therefore asserted against the shared source of truth both paths read:
	## structure_source_object_ids_for_team(team)[kind]. _seed_structures and
	## issue_construct now both stamp exactly that value for every kind, so a
	## constructed building agreeing with the table IS agreement with the seed
	## path.
	var buildable: Dictionary = sim.structure_build_rules_for_team(0)
	var sources: Dictionary = sim.structure_source_object_ids_for_team(0)
	var kind := ""
	var expected_id := ""
	for kind_value in buildable.keys():
		var candidate := String(kind_value)
		if candidate == "fortress" or candidate.contains("expansion") or candidate.contains("wall_hub"):
			continue
		var aliases: Variant = sources.get(candidate, [])
		if typeof(aliases) == TYPE_ARRAY and not (aliases as Array).is_empty():
			kind = candidate
			expected_id = String((aliases as Array)[0])
			break
	if kind == "":
		print("NON_FORTRESS_PARITY_DIAG buildable=%s sourced=%s" % [
			str(buildable.keys()), str(sources.keys())
		])
		print("SKIP %s_constructed_non_fortress_parity (no buildable non-fortress kind has a retail source id)" % _faction)
		_section_completed("constructed_non_fortress_parity")
		return

	var builder_ids: Array[int] = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entity(id)
		if int(row.get("team", -1)) == 0 and int(row.get("health", 0)) > 0 and bool(row.get("is_builder", false)):
			builder_ids = [id]
			break
	if builder_ids.is_empty():
		print("SKIP %s_constructed_non_fortress_parity (no builder)" % _faction)
		_section_completed("constructed_non_fortress_parity")
		return
	slice._grant_test_resources()
	var at := Vector2(sim.entity(builder_ids[0]).get("position", Vector2.ZERO))
	var built_id := 0
	for radius in [10.0, 16.0, 24.0, 34.0]:
		for step in range(8):
			var angle := TAU * float(step) / 8.0
			var order: Dictionary = sim.issue_construct(
				builder_ids, kind, at + Vector2(cos(angle), sin(angle)) * float(radius), false, 0
			)
			if bool(order.get("ok", false)):
				built_id = int(order.get("structure_id", 0))
				break
		if built_id != 0:
			break
	if not _check(
		"%s_a_%s_can_be_constructed" % [_faction, kind], built_id != 0, "no legal site accepted a %s" % kind
	):
		_section_completed("constructed_non_fortress_parity")
		return
	_check(
		"%s_built_%s_carries_the_seed_paths_retail_identity" % [_faction, kind],
		String(sim.structure(built_id).get("source_object_id", "")) == expected_id,
		"expected %s, got %s" % [expected_id, String(sim.structure(built_id).get("source_object_id", ""))]
	)
	# The identity must actually reach the footprint, or stamping it is cosmetic.
	# Compare against the same row with the id stripped: that is precisely the
	# state this lane found a constructed building in.
	var stripped: Dictionary = (sim.structure(built_id) as Dictionary).duplicate(true)
	stripped.erase("source_object_id")
	stripped.erase("footprint_radius_source")
	var with_identity := float(sim._structure_footprint_radius(sim.structure(built_id)))
	var without_identity := float(sim._structure_footprint_radius(stripped))
	print("NON_FORTRESS_FOOTPRINT kind=%s id=%s with=%.4f without=%.4f" % [
		kind, expected_id, with_identity, without_identity
	])
	_check(
		"%s_built_%s_footprint_comes_from_its_authored_geometry" % [_faction, kind],
		with_identity > 0.0,
		"the constructed %s resolved a non-positive footprint radius" % kind
	)
	_section_completed("constructed_non_fortress_parity")


func _upgrade_ids(commands: Array) -> Array:
	var ids: Array = []
	for command_value in commands:
		ids.append(String((command_value as Dictionary).get("upgrade_id", "")))
	ids.sort()
	return ids


func _command_surface_snapshot(slice, hud, structure_id: int) -> Dictionary:
	## The three radial pages of one structure, reduced to sorted
	## "<kind>:<id>" lists so two fortresses can be compared directly.
	var sim = slice.simulation
	slice.selected_structure_id = structure_id
	sim.selected_ids.clear()
	slice._selected_expansion_pad = {}
	var snapshot: Dictionary = {}
	for page_value in [RADIAL_PAGE_MAIN, RADIAL_PAGE_UPGRADES, RADIAL_PAGE_HEROES]:
		var page := String(page_value)
		var ids: Array = []
		for entry_value in await _radial_entries_for_page(hud, page):
			var entry: Dictionary = entry_value
			ids.append("%s:%s" % [String(entry.get("command_kind", "")), String(entry.get("id", ""))])
		ids.sort()
		snapshot[page] = {"ids": ids}
	hud.set_radial_page(RADIAL_PAGE_MAIN)
	return snapshot


func _check_mordor_fortress_interface_art() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	var failures: Array[String] = []
	if content_db == null:
		failures.append("ContentDB unavailable")
	else:
		for image_id in MORDOR_FORTRESS_IMAGE_IDS:
			var row: Dictionary = content_db.get_retail_ui_image(image_id)
			var resolved := String(content_db.resolve_retail_ui_image_path(image_id))
			if String(row.get("_origin", "")) != "interface-art" or resolved == "" or not FileAccess.file_exists(resolved):
				failures.append("%s origin=%s path=%s" % [image_id, String(row.get("_origin", "")), resolved])
	_check(
		"mordor_all_nine_fortress_image_ids_resolve_via_interface_art",
		failures.is_empty(),
		str(failures)
	)


func _check_fortress_radial_pages(slice, sim, hud, fortress: int) -> void:
	## Retail's fortress command wheel is three pages deep (see the header
	## transcription). Ours must offer the same doors, present the improvement
	## page with the pack's OWN localized label/icon/cost, and carry the created
	## hero on the hero page next to the faction's own heroes.
	if hud == null or not hud.has_method("set_radial_page"):
		_check("%s_fortress_radial_is_paged" % _faction, false, "RetailHud has no radial page API")
		return
	slice.selected_structure_id = fortress
	sim.selected_ids.clear()

	# --- Section 4: the main page is a MENU, not the whole command set --------
	var main_entries := await _radial_entries_for_page(hud, RADIAL_PAGE_MAIN)
	_check_radial_is_in_the_palantir_wheel(hud, RADIAL_PAGE_MAIN)
	var main_kinds: Dictionary = {}
	for entry_value in main_entries:
		main_kinds[String((entry_value as Dictionary).get("command_kind", ""))] = true
	_check(
		"%s_radial_main_page_hides_the_hero_slots" % _faction,
		not main_kinds.has("hero"),
		"the main page still lists hero recruit buttons: %s" % str(main_kinds.keys())
	)
	_check(
		"%s_radial_main_page_hides_the_improvement_slots" % _faction,
		not main_kinds.has("upgrade"),
		"the main page still lists fortress improvements: %s" % str(main_kinds.keys())
	)
	var page_targets: Dictionary = {}
	for entry_value in main_entries:
		var entry: Dictionary = entry_value
		if String(entry.get("command_kind", "")) == "page":
			page_targets[String(entry.get("id", ""))] = entry
	var offered_upgrades: Array = sim.structure_upgrade_commands(fortress)
	if not offered_upgrades.is_empty():
		_check(
			"%s_radial_main_page_opens_fortress_upgrades" % _faction,
			page_targets.has(RADIAL_PAGE_UPGRADES),
			"no Fortress Upgrades selector on the main page: %s" % str(page_targets.keys())
		)
	_check(
		"%s_radial_main_page_opens_the_hero_page" % _faction,
		page_targets.has(RADIAL_PAGE_HEROES),
		"no Heroes selector on the main page: %s" % str(page_targets.keys())
	)
	if _faction == "men":
		_check(
			"men_precompiled_page_selector_fallback_is_named",
			hud.retail_bind_diagnostics.any(func(note: String) -> bool: return note.begins_with("radial-page-selectors-fallback-precompiled-pack:")),
			str(hud.retail_bind_diagnostics)
		)
	if PAGE_SELECTOR_EXPECTATIONS.has(_faction):
		var content_db := root.get_node("ContentDB")
		for page_value in [RADIAL_PAGE_UPGRADES, RADIAL_PAGE_HEROES]:
			var selector_page := String(page_value)
			var expected: Array = (PAGE_SELECTOR_EXPECTATIONS[_faction] as Dictionary)[selector_page]
			var entry: Dictionary = page_targets.get(selector_page, {})
			_check(
				"%s_%s_selector_consumes_pack_ids_and_range" % [_faction, selector_page],
				String(entry.get("selector_command_id", "")) == String(expected[0])
					and String(entry.get("selector_label_id", "")) == String(expected[1])
					and int(entry.get("command_range_start", -1)) == int(expected[2])
					and int(entry.get("command_range_count", 0)) == int(expected[3]),
				str(entry)
			)
			var authored_label := String(content_db.get_retail_string(String(expected[1]), ""))
			_check(
				"%s_%s_selector_resolves_its_authored_label_id" % [_faction, selector_page],
				authored_label != "" and String(entry.get("label", "")) == authored_label,
				"id=%s expected='%s' got='%s'" % [String(expected[1]), authored_label, String(entry.get("label", ""))]
			)

	# --- Section 5: the improvement page, with retail label/icon/cost ---------
	if not offered_upgrades.is_empty():
		var upgrade_entries := await _radial_entries_for_page(hud, RADIAL_PAGE_UPGRADES)
		_check_radial_is_in_the_palantir_wheel(hud, RADIAL_PAGE_UPGRADES)
		var upgrade_by_id: Dictionary = {}
		var has_back := false
		for entry_value in upgrade_entries:
			var entry: Dictionary = entry_value
			if String(entry.get("command_kind", "")) == "back":
				has_back = true
				continue
			if String(entry.get("command_kind", "")) == "upgrade":
				upgrade_by_id[String(entry.get("id", ""))] = entry
		_check(
			"%s_radial_upgrade_page_has_a_back_button" % _faction,
			has_back,
			"the improvement page cannot be left (retail Command_RadialBack)"
		)
		for command_value in offered_upgrades:
			var upgrade_command: Dictionary = command_value
			var upgrade_id := String(upgrade_command.get("upgrade_id", ""))
			var entry: Dictionary = upgrade_by_id.get(upgrade_id, {})
			if not _check(
				"%s_radial_upgrade_page_offers_%s" % [_faction, upgrade_id],
				not entry.is_empty(),
				"the sim offers %s but the radial page does not" % upgrade_id
			):
				continue
			_check(
				"%s_radial_upgrade_%s_binds_its_pack_icon" % [_faction, upgrade_id],
				entry.get("icon") != null,
				"%s renders as text — its authored ButtonImage did not bind" % upgrade_id
			)
			var label := String(entry.get("label", ""))
			var faction_labels: Dictionary = RETAIL_UPGRADE_LABELS.get(_faction, {}) as Dictionary
			if faction_labels.has(upgrade_id):
				_check(
					"%s_radial_upgrade_%s_uses_the_retail_string" % [_faction, upgrade_id],
					label == String(faction_labels[upgrade_id]),
					"expected '%s', got '%s'" % [String(faction_labels[upgrade_id]), label]
				)
			_check(
				"%s_radial_upgrade_%s_states_its_cost" % [_faction, upgrade_id],
				int(entry.get("cost", -1)) == int(upgrade_command.get("cost", -1)),
				"radial cost %s does not match the compiled contract %s" % [
					str(entry.get("cost", -1)), str(upgrade_command.get("cost", -1))
				]
			)
			var faction_costs: Dictionary = RETAIL_UPGRADE_COSTS.get(_faction, {}) as Dictionary
			if faction_costs.has(upgrade_id):
				_check(
					"%s_radial_upgrade_%s_costs_the_retail_price" % [_faction, upgrade_id],
					int(entry.get("cost", -1)) == int(faction_costs[upgrade_id]),
					"expected %d, got %s" % [int(faction_costs[upgrade_id]), str(entry.get("cost", -1))]
				)

	# --- Section 6: the hero page carries the created hero --------------------
	var hero_entries := await _radial_entries_for_page(hud, RADIAL_PAGE_HEROES)
	_check_radial_is_in_the_palantir_wheel(hud, RADIAL_PAGE_HEROES)
	var hero_ids: Dictionary = {}
	var hero_back := false
	for entry_value in hero_entries:
		var entry: Dictionary = entry_value
		if String(entry.get("command_kind", "")) == "back":
			hero_back = true
			continue
		hero_ids[String(entry.get("id", ""))] = entry
	_check(
		"%s_radial_hero_page_has_a_back_button" % _faction,
		hero_back,
		"the hero page cannot be left (retail Command_RadialBack)"
	)
	var created_ids: Array = []
	for object_id in slice.producible_unit_runtimes.keys():
		if String(object_id).begins_with("CreateAHero__"):
			created_ids.append(String(object_id))
	_check(
		"%s_created_hero_is_on_the_roster" % _faction,
		not created_ids.is_empty(),
		"the seeded Create-a-Hero profile produced no fieldable document (%s)" % _cah_seed_error
	)
	for object_id in created_ids:
		var runtime_id := String(PlayableUnitAdapter._runtime_id(String(object_id)))
		var entry: Dictionary = hero_ids.get(runtime_id, {})
		if not _check(
			"%s_created_hero_is_purchasable_on_the_fortress" % _faction,
			not entry.is_empty(),
			"the created hero has no fortress radial button (page ids: %s)" % str(hero_ids.keys())
		):
			continue
		_check(
			"%s_created_hero_button_states_its_cost" % _faction,
			int(entry.get("cost", -1)) > 0,
			"the created hero's recruit button states no cost"
		)
		_check(
			"%s_created_hero_button_states_its_command_points" % _faction,
			int(entry.get("command_points", -1)) > 0,
			"the created hero's recruit button states no command point cost"
		)
	# Leave the wheel where a player would find it.
	hud.set_radial_page(RADIAL_PAGE_MAIN)

	_section_completed("radial_pages")
	# --- Section 7: the wheel with a LIVE production queue --------------------
	#
	# Every earlier radial check ran against an idle fortress, so the five
	# production queue chips were hidden and never took part in the overlap
	# assertions. They were authored under the palantir at panel-local
	# (60 + 40*i, 318) and the sixth retail command socket is at (148, 296),
	# 64px tall - a fortress that was training anything drew opaque chips through
	# a live command button. Queue real work and re-run the same gate.
	await _check_radial_with_a_live_queue(slice, hud, fortress)


func _check_radial_with_a_live_queue(slice, hud, fortress: int) -> void:
	var sim = slice.simulation
	slice._grant_test_resources()
	var producer := fortress
	var queued := 0
	for production_value in Array(sim.structure(fortress).get("production", [])):
		var result: Dictionary = sim.queue_unit(0, fortress, String(production_value))
		if bool(result.get("ok", false)):
			queued += 1
		if queued >= 2:
			break
	if queued == 0:
		# Any producing structure exercises the same chip surface.
		for structure_id in sim.structure_ids():
			var row: Dictionary = sim.structure(structure_id)
			if int(row.get("team", -1)) != 0 or int(row.get("health", 0)) <= 0:
				continue
			for production_value in Array(row.get("production", [])):
				var result: Dictionary = sim.queue_unit(0, structure_id, String(production_value))
				if bool(result.get("ok", false)):
					producer = structure_id
					queued += 1
				if queued >= 2:
					break
			if queued > 0:
				break
	if not _check(
		"%s_a_production_queue_can_be_populated" % _faction,
		queued > 0,
		"no player structure accepted a training order, so the chip surface is untested"
	):
		return
	sim.advance(5)
	slice.selected_structure_id = producer
	slice._sync_presentation()
	slice._refresh_hud()
	for _frame in range(4):
		await process_frame
	var visible_chips := 0
	for chip_value in hud.production_queue_buttons:
		if (chip_value as Button).is_visible_in_tree():
			visible_chips += 1
	print("FORTRESS_COMMAND_SURFACE queue producer=%d queued=%d visible_chips=%d chip0=%s" % [
		producer, queued, visible_chips,
		str((hud.production_queue_buttons[0] as Button).get_global_rect()) if not hud.production_queue_buttons.is_empty() else "<none>",
	])
	if not _check(
		"%s_production_chips_are_visible_while_training" % _faction,
		visible_chips > 0,
		"the queue was populated but no chip rendered, so the overlap gate below proves nothing"
	):
		return
	# The chips are inside the command panel, so the existing wheel gate now
	# includes them in its socket-intersection and containment assertions.
	_check_radial_is_in_the_palantir_wheel(hud, "%s+queue" % RADIAL_PAGE_MAIN)


func _radial_entries_for_page(hud, page: String) -> Array:
	hud.set_radial_page(page)
	for _frame in range(4):
		await process_frame
	return hud.radial_entries()


func _check_radial_is_in_the_palantir_wheel(hud, page: String) -> void:
	var panel := hud.command_panel as Control
	var outside: Array[String] = []
	var blank: Array[String] = []
	var overlaps: Array[String] = []
	var radial_overlaps: Array[String] = []
	if panel != null:
		var panel_rect := panel.get_global_rect()
		var visible_socket_buttons: Array[Button] = []
		_collect_visible_buttons(hud.command_panel, visible_socket_buttons)
		for index in hud._radial_buttons.size():
			var button := hud._radial_buttons[index] as Button
			if not panel_rect.encloses(button.get_global_rect()):
				outside.append("%s:%s" % [button.name, str(button.get_global_rect())])
			for socket_button in visible_socket_buttons:
				if button.get_global_rect().intersects(socket_button.get_global_rect()):
					overlaps.append("%s %s intersects %s %s" % [
						button.name, str(button.get_global_rect()),
						socket_button.name, str(socket_button.get_global_rect())
					])
			for other_index in range(index + 1, hud._radial_buttons.size()):
				var other := hud._radial_buttons[other_index] as Button
				if button.get_global_rect().intersects(other.get_global_rect()):
					radial_overlaps.append("%s %s intersects %s %s" % [
						button.name, str(button.get_global_rect()), other.name, str(other.get_global_rect())
					])
			var entry: Dictionary = hud._radial_entries[index]
			if entry.get("icon") == null and String(entry.get("label", "")).strip_edges() == "":
				blank.append(button.name)
	_check(
		"%s_radial_buttons_render_inside_the_palantir_wheel" % _faction,
		panel != null and not hud._radial_buttons.is_empty() and outside.is_empty(),
		"page=%s panel=%s outside=%s" % [page, str(panel.get_global_rect() if panel != null else Rect2()), str(outside)]
	)
	_check(
		"%s_radial_buttons_have_icon_or_honest_text" % _faction,
		blank.is_empty(),
		"page=%s blank=%s" % [page, str(blank)]
	)
	_check(
		"%s_radial_buttons_do_not_intersect_visible_socket_buttons" % _faction,
		overlaps.is_empty(),
		"page=%s overlaps=%s" % [page, str(overlaps)]
	)
	_check(
		"%s_radial_buttons_do_not_self_intersect" % _faction,
		radial_overlaps.is_empty(),
		"page=%s overlaps=%s" % [page, str(radial_overlaps)]
	)
	var visibility_before: Array[bool] = []
	for child in hud.command_socket_layer.get_children():
		if child is Button:
			visibility_before.append((child as Button).visible)
	var transition_count := int(hud._radial_socket_visibility_transitions)
	hud.sync_radial_commands(Vector2.ZERO, hud._radial_entries)
	hud.sync_radial_commands(Vector2.ZERO, hud._radial_entries)
	var visibility_after: Array[bool] = []
	for child in hud.command_socket_layer.get_children():
		if child is Button:
			visibility_after.append((child as Button).visible)
	_check(
		"%s_radial_socket_visibility_is_stable_across_frame_refreshes" % _faction,
		visibility_before == visibility_after
			and transition_count == int(hud._radial_socket_visibility_transitions),
		"page=%s before=%s after=%s transitions=%d->%d" % [
			page, str(visibility_before), str(visibility_after), transition_count,
			int(hud._radial_socket_visibility_transitions)
		]
	)


func _collect_visible_buttons(node: Node, into: Array[Button]) -> void:
	if node == null:
		return
	if node is Button and (node as Button).is_visible_in_tree():
		into.append(node as Button)
	for child in node.get_children():
		_collect_visible_buttons(child, into)


func _seed_created_hero() -> String:
	## One deterministic saved hero in this runner's OWN sandbox store, so the
	## created-hero fortress slot is gated rather than assumed. Returns "" on
	## success or the reason it could not be seeded.
	var content_db := root.get_node_or_null("ContentDB")
	if content_db == null:
		return "ContentDB autoload is unavailable"
	var system_value: Variant = content_db.get("cah_system_runtime")
	if typeof(system_value) != TYPE_DICTIONARY or (system_value as Dictionary).is_empty():
		return "the mounted packs ship no Create-a-Hero system runtime"
	var system: Dictionary = system_value
	for existing_value in CahHeroesScript.load_profiles():
		var existing: Dictionary = existing_value
		CahHeroesScript.delete_profile(String(existing.get("heroId", "")))
	var profile := CahHeroesScript.new_profile(system, "Gate Hero", 0, 0)
	var refusals: Array = CahHeroesScript.validate_profile(system, profile)
	if not refusals.is_empty():
		return "the seeded profile is invalid: %s" % str(refusals)
	return CahHeroesScript.save_profile(profile)


func _check_castle_upgrade_surface_shapes() -> void:
	## Retail authors a fortress improvement in THREE shapes, and the manifest's
	## fail-closed shape check must accept all three.
	##
	## 1. TRIGGER: buy `*Trigger`, a CastleUpgrade module converts it and hands a
	##    DIFFERENT upgrade to the castle (dwarvenfortress.ini:1096).
	## 2. PLAIN: buy an upgrade that applies to the fortress itself, no module
	##    behind it, so nothing is handed out (commandset.ini slots 8/9/11/13).
	## 3. SELF-GRANTING PASS-OUT: a CastleUpgrade module whose `TriggeredBy` and
	##    `Upgrade` are the SAME id. Angmar's House of Lamentation is authored
	##    exactly this way (angmarfortress.ini:1235-1238,
	##    `ModuleTag_PassOutHouseOfHealingUpgrade`). The same id in and out is not
	##    a no-op: the module's job is to propagate the upgrade the fortress was
	##    sold to every castle piece. Rejecting it as "buys nothing" made the
	##    whole Angmar manifest fail closed, so the slice would not boot at all.
	var trigger_row := {
		"upgradeId": "Upgrade_AngmarFortressIceWallsTrigger",
		"grantsUpgradeId": "Upgrade_AngmarFortressIceWalls",
		"cost": 500, "buildTimeSeconds": 30.0, "slot": 12,
		"commandId": "Command_PurchaseUpgradeAngmarFortressIceWalls",
	}
	var plain_row := {
		"upgradeId": "Upgrade_DwarvenFortressBanners", "grantsUpgradeId": "",
		"cost": 500, "buildTimeSeconds": 5.0, "slot": 8,
		"commandId": "Command_PurchaseUpgradeDwarvenFortressBanners",
	}
	var pass_out_row := {
		"upgradeId": "Upgrade_AngmarFortressHouseOfLamentation",
		"grantsUpgradeId": "Upgrade_AngmarFortressHouseOfLamentation",
		"cost": 1000, "buildTimeSeconds": 30.0, "slot": 11,
		"commandId": "Command_PurchaseUpgradeAngmarFortressHouseOfLamentation",
	}
	for row_case in [
		["trigger", trigger_row], ["plain", plain_row], ["self_granting_pass_out", pass_out_row],
	]:
		var shape_name := String(row_case[0])
		var error := ManifestScript._validate_structure_castle_upgrades(
			"AngmarFortressCitadel", {"upgrades": [row_case[1]]}
		)
		_check(
			"castle_upgrade_shape_%s_is_accepted" % shape_name,
			error == "",
			"retail's %s shape was rejected: %s" % [shape_name, error]
		)
	# A row that names no upgrade at all is still malformed and must fail closed;
	# widening for the pass-out shape must not turn the check off.
	var empty_error := ManifestScript._validate_structure_castle_upgrades(
		"AngmarFortressCitadel",
		{"upgrades": [{"upgradeId": "", "grantsUpgradeId": "", "cost": 1, "buildTimeSeconds": 1.0, "slot": 1, "commandId": "X"}]}
	)
	_check(
		"castle_upgrade_shape_empty_id_still_fails_closed",
		empty_error != "",
		"an upgrade row with no id was accepted"
	)


func _check_castle_upgrade_purchase_path() -> void:
	## Fixture-driven: proves the runtime half end to end against an authored
	## surface of the exact shape the compiler emits. (The claim that once stood
	## here — that the shipped packs carry no purchasable castleUpgrades surface
	## because the compiler skips OBJECT_UPGRADE buttons — is no longer true: the
	## packs now ship retail's whole upgrades page. The fixture is kept anyway so
	## the purchase path stays covered independently of whatever the mounted pack
	## happens to contain.)
	var sim = SimScript.new()
	sim._rules = {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
		"structure_castle_upgrades": {
			"fortress": {
				"upgrades": [{
					"upgradeId": "Upgrade_AngmarFortressIceWallsTrigger",
					"grantsUpgradeId": "Upgrade_AngmarFortressIceWalls",
					"cost": 500,
					"buildTimeSeconds": 30.0,
					"slot": 12,
					"commandId": "Command_PurchaseUpgradeAngmarFortressIceWalls",
					"labelId": "CONTROLBAR:AngmarFortressIceWalls",
					"tooltipId": "CONTROLBAR:ToolTipAngmarFortressIceWalls",
					"buttonImageId": "BAFortress_IceWalls",
				}, {
					# The PLAIN shape: retail's Banners button
					# (commandset.ini:4107 slot 8, commandbutton.ini:13695) buys
					# an upgrade that applies to the fortress itself, with no
					# CastleUpgrade pass-out module. Four of the six buttons on
					# the dwarven upgrades page are authored this way, so a
					# surface that only accepts the trigger shape leaves them
					# unbuyable.
					"upgradeId": "Upgrade_DwarvenFortressBanners",
					"grantsUpgradeId": "",
					"cost": 500,
					"buildTimeSeconds": 5.0,
					"slot": 8,
					"commandId": "Command_PurchaseUpgradeDwarvenFortressBanners",
					"labelId": "CONTROLBAR:PurchaseUpgradeDwarvenFortressBanners",
					"tooltipId": "CONTROLBAR:ToolTipPurchaseUpgradeDwarvenFortressBanners",
					"buttonImageId": "BDFortress_Banners",
				}],
			},
		},
	}
	sim.setup({}, {})
	sim.ai_enabled = false
	_check("castle_upgrade_surface_compiles", String(sim.configuration_error) == "", String(sim.configuration_error))
	var contracts: Dictionary = sim.structure_upgrade_contracts_for_team(0)
	var contract: Dictionary = contracts.get("Upgrade_AngmarFortressIceWallsTrigger", {})
	_check("castle_upgrade_registers_a_contract", not contract.is_empty(), "no contract for the trigger upgrade")
	_check(
		"castle_upgrade_contract_records_the_granted_upgrade",
		String(contract.get("grants_upgrade_id", "")) == "Upgrade_AngmarFortressIceWalls",
		str(contract)
	)

	# The trigger -> real upgrade hop itself, read from the same opaque-authored
	# moduleContracts row the packs already ship (angmarfortresscitadel.json,
	# module "CastleUpgrade", runtimeStatus "deferred").
	sim.register_castle_upgrade_grants("AngmarFortressCitadel", [{
		"module": "CastleUpgrade",
		"tag": "ModuleTag_PassOutAngmarStoneworkUpgrade",
		"fields": {
			"TriggeredBy": {"authored": "Upgrade_AngmarFortressIceWallsTrigger"},
			"Upgrade": {"authored": "Upgrade_AngmarFortressIceWalls"},
			"WallUpgradeRadius": {"authored": "ANGMAR_FORTRESS_WALL_EFFECTIVE_RADIUS"},
		},
	}])
	var grants: Array = sim.castle_upgrade_grants_for("Upgrade_AngmarFortressIceWallsTrigger")
	_check("castle_upgrade_module_indexes_the_grant", grants.size() == 1, str(grants))

	var fortress := int(sim.fortress_id(0))
	if not _check("castle_upgrade_fixture_has_a_fortress", fortress != 0, "no team-0 fortress"):
		return
	var building: Dictionary = sim.structures[fortress]
	building["structure_kind"] = "fortress"
	# One castle piece so the pass-out is observable beyond the fortress itself.
	var piece_id := 90001
	sim.structures[piece_id] = {
		"id": piece_id,
		"team": 0,
		"kind": "structure",
		"structure_kind": "castle_piece",
		"position": building.get("position", Vector2.ZERO),
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
	}
	building["castle_piece_structure_ids"] = [piece_id]

	var offered: Array = sim.structure_upgrade_commands(fortress)
	var offered_ids: Array = []
	for row_value in offered:
		offered_ids.append(String((row_value as Dictionary).get("upgrade_id", "")))
	_check(
		"castle_upgrade_is_offered_on_the_fortress",
		offered_ids.has("Upgrade_AngmarFortressIceWallsTrigger"),
		str(offered_ids)
	)
	# The plain (grant-less) improvement must be sellable too, and must carry its
	# own authored price rather than being filed as non-purchasable.
	_check(
		"plain_fortress_improvement_is_offered_on_the_fortress",
		offered_ids.has("Upgrade_DwarvenFortressBanners"),
		str(offered_ids)
	)
	var plain_contract: Dictionary = contracts.get("Upgrade_DwarvenFortressBanners", {})
	_check(
		"plain_fortress_improvement_keeps_its_authored_price",
		int(plain_contract.get("cost", -1)) == 500
			and String(plain_contract.get("grants_upgrade_id", "")) == "",
		str(plain_contract)
	)

	sim.team_resources[0] = 10000
	var queued: Dictionary = sim.queue_structure_upgrade(0, fortress, "Upgrade_AngmarFortressIceWallsTrigger")
	_check("castle_upgrade_purchase_is_accepted", bool(queued.get("ok", false)), String(queued.get("reason", "")))
	var duration := int(contract.get("duration_ticks", 1))
	for _tick in range(duration + 2):
		sim.tick()
	var completed: Array = (sim.structure(fortress) as Dictionary).get("completed_upgrades", [])
	_check(
		"castle_upgrade_trigger_completes",
		completed.has("Upgrade_AngmarFortressIceWallsTrigger"),
		str(completed)
	)
	_check(
		"castle_upgrade_grants_the_real_upgrade_to_the_fortress",
		completed.has("Upgrade_AngmarFortressIceWalls"),
		str(completed)
	)
	var piece_completed: Array = (sim.structure(piece_id) as Dictionary).get("completed_upgrades", [])
	_check(
		"castle_upgrade_passes_the_real_upgrade_to_the_castle_pieces",
		piece_completed.has("Upgrade_AngmarFortressIceWalls"),
		str(piece_completed)
	)
	# The improvement must not blank the fortress's command set (it is not a
	# level chain step).
	_check(
		"castle_upgrade_does_not_swap_the_command_set",
		int((sim.structure(fortress) as Dictionary).get("level", 1)) == 1,
		"fortress level moved on a non-level improvement"
	)

## Minimal synthetic unit rule (the shape the script-world harness uses) so the
## fixture sim boots its base loop without a converted pack.
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


func _check(name: String, ok: bool, detail: String) -> bool:
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	# Liveness pin: every section this run committed to must have reported that
	# it finished. Without this a coroutine that aborted mid-section simply
	# contributes fewer checks and the run still prints green.
	var missing: Array[String] = []
	for name in _sections_expected:
		if not _sections_completed.has(name):
			missing.append(name)
	if not missing.is_empty():
		failed += 1
		print("FAIL runner_ran_every_section :: started %s but never finished %s — a section aborted silently" % [
			str(_sections_expected), str(missing)
		])
	print("RESULT fortress_command_surface faction=%s passed=%d failed=%d sections=%s" % [
		_faction, passed, failed, str(_sections_completed)
	])
	quit(0 if failed == 0 else 1)
