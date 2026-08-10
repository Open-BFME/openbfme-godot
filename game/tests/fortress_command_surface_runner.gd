extends SceneTree
## Fortress command-surface gate (bugs A/B/C of the castle system).
##
## Retail oracle (PURE RotWK retail tree,
## .private/retail-work/editions/rotwk/cache/effective-assets/data/ini):
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
##      castleUpgrades surface yet (see .private/scratch/opus16-fortress-report.md).
##
## Run:
##   OPENBFME_CONTENT=<repo>/.private/content-packs godot --headless --path game \
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
	await _check_fortress_radial_pages(slice, sim, hud, fortress)
	_finish()


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
			page_targets[String(entry.get("id", ""))] = String(entry.get("label", ""))
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

	# --- Section 5: the improvement page, with retail label/icon/cost ---------
	if not offered_upgrades.is_empty():
		var upgrade_entries := await _radial_entries_for_page(hud, RADIAL_PAGE_UPGRADES)
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


func _radial_entries_for_page(hud, page: String) -> Array:
	hud.set_radial_page(page)
	for _frame in range(4):
		await process_frame
	return hud.radial_entries()


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
	print("RESULT fortress_command_surface faction=%s passed=%d failed=%d" % [_faction, passed, failed])
	quit(0 if failed == 0 else 1)
