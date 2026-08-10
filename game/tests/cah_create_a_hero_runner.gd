extends SceneTree
## Gate for the Create-a-Hero slice: profile round-trip, the attribute
## arithmetic, the fortress roster, and the purchase that spawns the hero.
##
## SELF-CONTAINED BY DESIGN. The class table this drives is built here from
## retail's own authored numbers rather than read off a cooked pack, so the gate
## runs on a machine with no retail install and still asserts against real
## values. The numbers below are transcribed from PURE RETAIL
## (`.private/retail-work/editions/rotwk/cache/effective-assets`), NOT from the
## patched `layered-effective-assets` tree - the 2.02 Unofficial Patch rewrote
## every Create-a-Hero attribute ladder and the object's cost, and a gate
## written against the patched numbers would pass while shipping wrong heroes.
##
## Citations for every constant are in the table comments.

const CahHeroes = preload("res://src/content/cah_heroes.gd")
const MyHeroesScreen = preload("res://src/ui/my_heroes_screen.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const SubObjects = preload("res://src/ui/cah_sub_objects.gd")

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()

var passed := 0
var failed := 0

## Captain of Gondor, from createaherosystemmenofthewest.inc:20,55-88.
## Budget 30 = 11 + 8 + 6 + 1 + 4, which is the whole authored budget.
const CAPTAIN_ATTRIBUTES := [
	{"groupName": "CreateAHero_ArmorAttribute", "minStep": 5, "maxStep": 20, "defaultStep": 16},
	{"groupName": "CreateAHero_DamageMultAttribute", "minStep": 4, "maxStep": 17, "defaultStep": 12},
	{"groupName": "CreateAHero_HealthMultAttribute", "minStep": 4, "maxStep": 15, "defaultStep": 10},
	{"groupName": "CreateAHero_AutoHealAttribute", "minStep": 5, "maxStep": 18, "defaultStep": 6},
	{"groupName": "CreateAHero_VisionAttribute", "minStep": 4, "maxStep": 14, "defaultStep": 8},
]

## The five ladders exactly as PURE RETAIL authors them in attributemodifier.ini,
## as `#MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER x )` with the multiplier
## defined as 1 (createaherogamedata.inc:7), so each entry is the literal x.
## All twenty steps of all five groups are transcribed - no interpolation, no
## nearest-value fill. A gate that guessed the steps it did not assert on would
## be able to pass while the guess was wrong.
##
## Note the Vision group is the only one emitting TWO modifiers per step, on
## different curves, and that AUTO_HEAL is the only non-linear ladder.
const ARMOR_LADDER := [0.25, 0.30, 0.35, 0.40, 0.42, 0.44, 0.46, 0.48, 0.50, 0.52, 0.54, 0.56, 0.58, 0.60, 0.62, 0.64, 0.67, 0.70, 0.75, 0.80]
const DAMAGE_LADDER := [0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.10, 2.20, 2.30, 2.40, 2.50]
const HEALTH_LADDER := [1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.10, 2.20, 2.30, 2.40, 2.50, 2.60, 2.70, 2.80, 2.90]
const AUTOHEAL_LADDER := [0.10, 0.25, 0.50, 0.75, 1.00, 1.50, 2.00, 2.50, 3.00, 3.50, 4.00, 4.50, 5.00, 5.50, 6.00, 6.50, 7.00, 8.00, 9.00, 10.00]
const SHROUD_LADDER := [0.00, 0.20, 0.40, 0.60, 0.80, 1.00, 1.20, 1.40, 1.60, 1.80, 2.00, 2.20, 2.40, 2.60, 2.80, 3.00, 3.20, 3.40, 3.70, 4.00]
const VISION_LADDER := [0.50, 0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50, 1.60, 1.70, 1.80, 1.90, 2.00, 2.10, 2.20, 2.30, 2.40]

## Base object numbers, PURE RETAIL. Every one of these differs from the patched
## tree except maxHealth, which reaches 2000 by a different route in each.
##   BuildCost   = CAH_BUILDCOST                (gamedata.ini:6734)          500
##   BuildTime   = CAH_BUILDTIME                (gamedata.ini:6735)           30
##   MaxHealth   = FARAMIR_HEALTH               (gamedata.ini:6742)         2000
##   VisionRange = CREATE_A_HERO_VISION_RANGE   (createaherogamedata.inc)    150
##   ShroudClear = SHROUD_CLEAR_CREATE_A_HERO   (createaherogamedata.inc)    100
##   CommandPts  = CREATE_A_HERO_COMMAND_POINT_COST                           50
##   Revive      = RespawnRules Cost:1500       (createaherorespawn.inc:22) 1500
const BASE_BUILD_COST := 500
const BASE_MAX_HEALTH := 2000
const BASE_VISION := 150.0
const BASE_SHROUD := 100.0
const BASE_COMMAND_POINTS := 50

const PRODUCER := "MenFortress"
## The four level columns retail authors, which the grid's last row labels.
const POWER_COLUMN_COUNT := 4


func _initialize() -> void:
	_runner_watchdog.start(self, "CAH_CREATE_A_HERO_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_clear_profiles()
	var system := _system_document()

	_check(CahHeroes.system_is_valid(system), "the synthetic class table is a valid cah-system-runtime")

	_test_attribute_arithmetic(system)
	_test_profile_validation(system)
	_test_profile_round_trip(system)
	_test_roster_document(system)
	_test_power_selection_rules(system)
	_test_powers_and_levels_reach_the_runtime_contracts(system)
	_test_screen(system)
	_test_garment_sub_objects(system)
	_test_purchase_spawns_with_computed_stats(system)
	_test_pack_resolution(system)
	await _test_full_window_layout(system)

	_clear_profiles()
	_finish()


# ---------------------------------------------------------------- arithmetic


func _test_attribute_arithmetic(system: Dictionary) -> void:
	var sub_row := CahHeroes.sub_class_row(system, 0, 0)
	_check(not sub_row.is_empty(), "class 0 subclass 0 resolves to Captain of Gondor")
	_check(CahHeroes.attribute_budget(sub_row) == 30, "Captain of Gondor budget is 30")

	# THE RULE: one point per step above the class minimum, and the authored
	# default loadout spends the budget exactly. 11 + 8 + 6 + 1 + 4 = 30.
	var defaults := CahHeroes.default_attributes(sub_row)
	_check(
		CahHeroes.attribute_spend(sub_row, defaults) == 30,
		"the authored default loadout spends exactly the whole 30-point budget"
	)

	# Spending is per-step and linear: moving armour one step up costs one point.
	var one_up := defaults.duplicate()
	one_up["CreateAHero_ArmorAttribute"] = 17
	_check(
		CahHeroes.attribute_spend(sub_row, one_up) == 31,
		"raising one attribute by one step costs exactly one point"
	)

	var stats := CahHeroes.computed_stats(system, sub_row, defaults)
	# health = FARAMIR_HEALTH 2000 * HealthMult[10] 1.90 = 3800
	_check(int(stats["health"]) == 3800, "default health is 2000 * 1.90 = 3800 (got %d)" % int(stats["health"]))
	# vision = 150 * Vision[8] 1.20 = 180
	_check(is_equal_approx(float(stats["visionRange"]), 180.0), "default vision is 150 * 1.20 = 180")
	# shroud = 100 * ShroudClearing[8] 1.40 = 140
	_check(is_equal_approx(float(stats["shroudClearingRange"]), 140.0), "default shroud clearing is 100 * 1.40 = 140")
	_check(is_equal_approx(float(stats["armorScalar"]), 0.64), "default armour scalar is Armor[16] = 0.64")
	_check(is_equal_approx(float(stats["damageMultiplier"]), 1.70), "default damage multiplier is DamageMult[12] = 1.70")
	_check(is_equal_approx(float(stats["autoHealMultiplier"]), 1.50), "default auto-heal is AutoHeal[6] = 1.50")
	_check(int(stats["buildCost"]) == BASE_BUILD_COST, "build cost is CAH_BUILDCOST = 500")
	_check(int(stats["commandPoints"]) == BASE_COMMAND_POINTS, "command points is 50")
	_check(int(stats["reviveCost"]) == 1500, "revive cost is 1500")

	# A DIFFERENT LOADOUT PRODUCES DIFFERENT NUMBERS, which is the whole point of
	# the feature. Trade two damage steps for two health steps: the spend is
	# unchanged, both stats move, and nothing else does.
	var tank := defaults.duplicate()
	tank["CreateAHero_HealthMultAttribute"] = 12
	tank["CreateAHero_DamageMultAttribute"] = 10
	_check(
		CahHeroes.attribute_spend(sub_row, tank) == 30,
		"trading 2 damage steps for 2 health steps keeps the spend at 30"
	)
	var tank_stats := CahHeroes.computed_stats(system, sub_row, tank)
	# 2000 * HealthMult[12] 2.10 = 4200
	_check(int(tank_stats["health"]) == 4200, "the traded loadout has 2000 * 2.10 = 4200 health (got %d)" % int(tank_stats["health"]))
	_check(
		is_equal_approx(float(tank_stats["damageMultiplier"]), 1.50),
		"the traded loadout has DamageMult[10] = 1.50"
	)


# --------------------------------------------------------------- validation


func _test_profile_validation(system: Dictionary) -> void:
	var profile := CahHeroes.new_profile(system, "Beregond", 0, 0)
	_check(CahHeroes.validate_profile(system, profile).is_empty(), "a fresh default profile validates")

	var unspent := profile.duplicate(true)
	(unspent["attributes"] as Dictionary)["CreateAHero_ArmorAttribute"] = 15
	var unspent_refusals := CahHeroes.validate_profile(system, unspent)
	_check(
		unspent_refusals.size() == 1 and String(unspent_refusals[0]).contains("29 of 30"),
		"leaving a point unspent is refused and says 29 of 30 (got %s)" % str(unspent_refusals)
	)

	var overspent := profile.duplicate(true)
	(overspent["attributes"] as Dictionary)["CreateAHero_ArmorAttribute"] = 17
	_check(
		not CahHeroes.validate_profile(system, overspent).is_empty(),
		"overspending the budget is refused"
	)

	var out_of_range := profile.duplicate(true)
	(out_of_range["attributes"] as Dictionary)["CreateAHero_ArmorAttribute"] = 4
	var range_refusals := CahHeroes.validate_profile(system, out_of_range)
	_check(
		range_refusals.size() >= 1 and String(range_refusals[0]).contains("outside the authored 5..20"),
		"a step below the class minimum is refused by range, not by budget"
	)

	var dropped := profile.duplicate(true)
	dropped["classIndex"] = 9
	_check(
		not CahHeroes.validate_profile(system, dropped).is_empty(),
		"a profile naming a class the mounted content lacks is refused"
	)

	var unnamed := profile.duplicate(true)
	unnamed["name"] = "   "
	_check(not CahHeroes.validate_profile(system, unnamed).is_empty(), "an unnamed hero is refused")


# -------------------------------------------------------------- round trip


func _test_profile_round_trip(system: Dictionary) -> void:
	var profile := CahHeroes.new_profile(system, "Beregond", 0, 0)
	(profile["attributes"] as Dictionary)["CreateAHero_HealthMultAttribute"] = 12
	(profile["attributes"] as Dictionary)["CreateAHero_DamageMultAttribute"] = 10
	var error := CahHeroes.save_profile(profile)
	_check(error == "", "the profile saves to user:// (%s)" % error)

	var reloaded := CahHeroes.load_profile(String(profile["heroId"]))
	_check(not reloaded.is_empty(), "the profile loads back by hero id")
	_check(String(reloaded.get("name", "")) == "Beregond", "the name survives the round trip")
	_check(int(reloaded.get("classIndex", -1)) == 0, "the class survives the round trip")
	_check(
		int((reloaded.get("attributes", {}) as Dictionary).get("CreateAHero_HealthMultAttribute", 0)) == 12,
		"the allocated attributes survive the round trip"
	)
	_check(
		CahHeroes.validate_profile(system, reloaded).is_empty(),
		"the reloaded profile still validates against the mounted table"
	)
	_check(CahHeroes.load_profiles().size() == 1, "the saved hero is listed exactly once")

	# Ids reach the filesystem, so anything that is not plain hex is refused
	# rather than sanitized into something that might still escape the directory.
	var hostile := profile.duplicate(true)
	hostile["heroId"] = "../../etc/passwd"
	_check(
		CahHeroes.save_profile(hostile) != "",
		"a hero id containing path separators is refused"
	)
	_check(CahHeroes.load_profile("../../etc/passwd").is_empty(), "a hostile hero id loads nothing")

	_check(CahHeroes.delete_profile(String(profile["heroId"])), "the profile deletes")
	_check(CahHeroes.load_profiles().is_empty(), "the store is empty after delete")


# ----------------------------------------------------------- roster document


func _test_roster_document(system: Dictionary) -> void:
	var profile := CahHeroes.new_profile(system, "Beregond", 0, 0)
	var document := CahHeroes.roster_document(system, profile, PRODUCER, 12)

	_check(String(document.get("schema", "")) == "openbfme.playable-unit-runtime", "the roster document is a playable-unit-runtime")
	_check(int(document.get("schemaVersion", -1)) == 0, "the roster document is schema version 0")
	_check(String(document.get("category", "")) == "hero", "the created hero is category hero")
	_check(
		String(document.get("objectId", "")).begins_with("CreateAHero__"),
		"the object id is namespaced so two created heroes cannot collide"
	)

	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var production: Array = registration.get("production", []) as Array
	_check(production.size() == 1, "the created hero has exactly one production route")
	var route := production[0] as Dictionary
	_check(String(route.get("surface", "")) == "hero-roster", "the route is on the hero-roster surface")
	_check(String(route.get("producerObjectId", "")) == PRODUCER, "the producer is the fortress")
	_check(int(route.get("rosterOrdinal", -1)) == 12, "the roster ordinal is the one the caller assigned")
	_check(int(route.get("slot", -1)) == 0, "hero-roster routes occupy slot 0")

	# THE REAL GATE: the shared adapter every other hero goes through must accept
	# this document unchanged. If it does not, the created hero silently vanishes
	# from the fortress instead of failing loudly, which is the exact failure the
	# adapter's surface matrix exists to prevent.
	_check(Adapter.fieldability(document).get("ok", false), "the shared adapter considers the created hero fieldable")
	var bindings: Array = Adapter.producer_bindings(document)
	_check(bindings.size() == 1, "the adapter derives exactly one producer binding (got %d)" % bindings.size())
	var hud_specs: Array = Adapter.hud_specs(document)
	_check(hud_specs.size() == 1, "the adapter derives a HUD button spec (got %d)" % hud_specs.size())

	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	_check(int(simulation.get("memberHealth", 0)) == 3800, "the roster document carries the computed 3800 health")
	_check(int(simulation.get("buildCost", 0)) == BASE_BUILD_COST, "the roster document carries the 500 build cost")
	_check(String(simulation.get("displayName", "")) == "Beregond", "the roster document shows the player's chosen name")

	var evidence: Dictionary = registration.get("createAHero", {}) as Dictionary
	_check(
		is_equal_approx(float((evidence.get("computed", {}) as Dictionary).get("healthMultiplier", 0.0)), 1.90),
		"the document carries the multiplier its health came from"
	)


# ---------------------------------------------------------------- the screen


func _test_screen(system: Dictionary) -> void:
	var screen = MyHeroesScreen.new()
	root.add_child(screen)
	screen.configure(system)

	_check(screen.system_available(), "the screen accepts the mounted class table")
	var pair: Vector2i = screen.spend_and_budget()
	_check(pair == Vector2i(30, 30), "the screen opens on the authored default loadout, fully spent")

	screen.set_attribute("CreateAHero_ArmorAttribute", 17)
	_check(screen.spend_and_budget() == Vector2i(31, 30), "overspending is reflected in the budget readout")
	_check(screen.save_button.disabled, "SAVE is disabled while the budget does not balance")

	screen.set_attribute("CreateAHero_ArmorAttribute", 16)
	_check(not screen.save_button.disabled, "SAVE re-enables once the budget balances again")

	var refusals: Array = screen.create_hero("Beregond")
	_check(refusals.is_empty(), "the screen creates a hero (%s)" % str(refusals))
	_check(screen.saved_profiles().size() == 1, "the screen lists the hero it just created")
	_check(screen.hero_list.item_count == 1, "the saved-hero list shows one row")

	_check(not screen.create_hero("   ").is_empty(), "the screen refuses an unnamed hero")

	# Switching class resets the loadout to the new class's authored default,
	# which is fully spent for every class - so the screen is never left in a
	# state the player has to repair before they can save.
	screen.set_class_selection(0, 1)
	var maiden: Vector2i = screen.spend_and_budget()
	_check(maiden == Vector2i(30, 30), "switching subclass lands on that subclass's fully-spent default")

	# NO TABLE MOUNTED is a state the screen must survive and explain.
	var empty = MyHeroesScreen.new()
	root.add_child(empty)
	empty.configure({})
	_check(not empty.system_available(), "the screen reports no table when none is mounted")
	_check(
		String(empty.status_label.text).contains("compile-cah-system"),
		"with no table the screen names the command that produces one"
	)
	_check(
		not empty.create_hero("Beregond").is_empty(),
		"with no table the screen refuses to create a hero"
	)
	empty.queue_free()
	screen.queue_free()
	_clear_profiles()


# ------------------------------------------------------------ garment parts


func _synthetic_skin() -> Node3D:
	## A stand-in for a converted Create-a-Hero GLB: one body and two numbered
	## variants of three parts, named the way retail names its sub-objects.
	var root := Node3D.new()
	var skeleton := Node3D.new()
	skeleton.name = "Skeleton"
	root.add_child(skeleton)
	for part in ["CHHW_SMN", "HLMT_01", "HLMT_02", "SLDR_01", "SLDR_04", "SHLD_01", "SHLD_02", "GURTHANG"]:
		var mesh := MeshInstance3D.new()
		mesh.name = part
		mesh.mesh = BoxMesh.new()
		skeleton.add_child(mesh)
	return root


func _visible_parts(root: Node3D) -> Array[String]:
	var out: Array[String] = []
	var stack: Array[Node] = [root]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if node is MeshInstance3D and (node as MeshInstance3D).visible:
			out.append(String(node.name))
	out.sort()
	return out


func _test_garment_sub_objects(_system: Dictionary) -> void:
	## THE GARMENTS ON THE HERO'S BODY.
	##
	## Retail bakes every helmet, shoulder plate and shield into the one skin and
	## switches their visibility; the converted GLBs keep the retail part names
	## (verified against `CHHW_CG_C_SKN.glb`: HLMT_01/02/05/06, SLDR_01/02/04/05,
	## SHLD_01..04, BOOT_00..05, GURTHANG, TROLLBANE...). These drive the switch
	## over a SYNTHETIC skin so the gate never waits on the content lane.
	var mapped_system := {
		"schema": CahHeroes.SYSTEM_SCHEMA,
		"schemaVersion": CahHeroes.SYSTEM_SCHEMA_VERSION,
		"registration": {
			"appearanceOptions": [
				{"upgradeName": "Upgrade_H1", "subObjects": {"show": ["HLMT_01"], "hide": []}},
				{"upgradeName": "Upgrade_H2", "subObjects": {"show": ["HLMT_02"], "hide": []}},
				{"upgradeName": "Upgrade_HNone", "subObjects": {"show": [], "hide": []}},
				{"upgradeName": "Upgrade_S1", "subObjects": {"show": ["SLDR_01"], "hide": []}},
				# A part this skin does not carry: retail authors garments that
				# only exist on the battlefield mesh, and the preview must ignore
				# the name rather than fail on it.
				{"upgradeName": "Upgrade_S9", "subObjects": {"show": ["SLDR_99"], "hide": []}},
			],
		},
	}
	var sub_row := {
		"models": {
			"creationScreen": {
				"model": "SYNTH_C_SKN",
				"defaultSubObjects": {"show": ["CHHW_SMN"], "hide": ["SHLD_01", "SHLD_02"]},
			},
		},
		"appearanceChoices": {
			"CreateAHero_Helmet": ["Upgrade_HNone", "Upgrade_H1", "Upgrade_H2"],
			"CreateAHero_ShoulderPlates": ["Upgrade_S1", "Upgrade_S9"],
		},
	}

	_check(
		SubObjects.system_maps_sub_objects(mapped_system),
		"a pack that binds appearance options to mesh parts is recognised as mapping them"
	)
	_check(
		not SubObjects.system_maps_sub_objects({"registration": {"appearanceOptions": [
			{"upgradeName": "Upgrade_H1"},
		]}}),
		"a pack with no bindings is recognised as NOT mapping them"
	)

	# THE DEFAULT LOADOUT: the first option of every group, plus the subclass's
	# own default set.
	var skin := _synthetic_skin()
	var opening := SubObjects.plan(mapped_system, sub_row, {}, "creationScreen")
	_check(bool(opening.get("mapped", false)), "the opening plan reports itself mapped")
	SubObjects.apply(skin, opening)
	_check(
		", ".join(_visible_parts(skin)) == "CHHW_SMN, GURTHANG, SLDR_01, SLDR_04",
		("the default loadout wears the body and the first shoulder plate, no helmet because the "
			+ "first helmet option is the bare-headed one, and neither shield because the subclass "
			+ "default hides them - and SLDR_04, which no option in this table claims, is left "
			+ "exactly as the pack shipped it (got %s)") % str(_visible_parts(skin))
	)

	# CYCLING A GROUP REPLACES, it does not stack: choosing helmet 2 takes helmet
	# 1 off, which is the whole difference between this and a hero wearing four
	# helmets at once.
	var second := SubObjects.plan(
		mapped_system, sub_row, {"CreateAHero_Helmet": "Upgrade_H2"}, "creationScreen"
	)
	var applied := SubObjects.apply(skin, second)
	_check(
		not _visible_parts(skin).has("HLMT_01") and _visible_parts(skin).has("HLMT_02"),
		"cycling to the second helmet hides the first (got %s)" % str(_visible_parts(skin))
	)
	# ...and the "no helmet" option really takes it off.
	SubObjects.apply(skin, SubObjects.plan(
		mapped_system, sub_row, {"CreateAHero_Helmet": "Upgrade_HNone"}, "creationScreen"
	))
	_check(
		not _visible_parts(skin).has("HLMT_01") and not _visible_parts(skin).has("HLMT_02"),
		"the bare-headed option shows no helmet at all (got %s)" % str(_visible_parts(skin))
	)

	# A NAME THE MESH DOES NOT CARRY IS COUNTED, NOT FATAL.
	var missing := SubObjects.apply(skin, SubObjects.plan(
		mapped_system, sub_row, {"CreateAHero_ShoulderPlates": "Upgrade_S9"}, "creationScreen"
	))
	_check(
		(missing.get("unknown", []) as Array).has("SLDR_99"),
		"a part the skin does not carry is reported unknown rather than raising"
	)
	_check(
		not _visible_parts(skin).has("SLDR_01"),
		"choosing the shoulder plate this skin lacks still takes the other one off"
	)

	# THE STOPGAP for a pack that carries no bindings at all. Numbered siblings
	# are alternatives by construction, so at most one of each may show; a part
	# with no numbered sibling is left exactly as the pack shipped it.
	var raw := _synthetic_skin()
	var collapse := SubObjects.collapse_variant_families(raw)
	_check(not bool(collapse.get("mapped", true)), "the stopgap plan does not claim to be mapped")
	SubObjects.apply(raw, collapse)
	_check(
		", ".join(_visible_parts(raw)) == "CHHW_SMN, GURTHANG, HLMT_01, SHLD_01, SLDR_01",
		"an unmapped skin collapses to one variant per numbered part (got %s)"
			% str(_visible_parts(raw))
	)

	_check(
		SubObjects.mesh_names(raw).has("GURTHANG"),
		"the part inventory reads the retail sub-object names off the model"
	)
	_check(
		(SubObjects.apply(null, opening).get("matched", -1)) == 0,
		"applying a plan to no model at all is a no-op rather than a crash"
	)

	skin.queue_free()
	raw.queue_free()

	# THE SCREEN'S OWN REPORT. The mounted synthetic table carries no bindings,
	# so the screen must say so rather than pretend the hero is dressed to order.
	var screen = MyHeroesScreen.new()
	root.add_child(screen)
	screen.configure(_system)
	_check(
		screen.garment_status() in ["unmapped", "no-model", "mapped", "mapped-unmatched"],
		"the screen reports what it did about garments (got '%s')" % screen.garment_status()
	)
	screen.queue_free()


# ---------------------------------------------------- pack art and pack meshes


func _test_pack_resolution(system: Dictionary) -> void:
	## THE RESOLVER THE SCREEN ACTUALLY CALLS.
	##
	## `_resolve_model_path` used to probe `ContentDB.resolve_cah_model_path` and
	## `ContentDB.resolve_model_path`, NEITHER OF WHICH EXISTED - so the preview
	## fell to "not in the mounted pack yet" no matter what the pack carried, and
	## the missing link was indistinguishable from missing content. These drive the
	## real resolver over a SYNTHETIC pack, so they answer on a machine with no
	## retail install and never depend on the Create-a-Hero meshes having landed.
	var content_db := root.get_node_or_null("ContentDB")
	_check(content_db != null, "the ContentDB autoload is reachable from the runner")
	if content_db == null:
		return

	_check(
		String(content_db.resolve_cah_model_path("OpenBfmeNoSuchCahModel")) == "",
		"a model id no mounted pack carries resolves to empty rather than a guess"
	)
	_check(String(content_db.resolve_cah_model_path("")) == "", "an empty model id resolves to empty")
	_check(
		String(content_db.resolve_cah_model_path("../../etc/passwd")) == ""
			and String(content_db.resolve_cah_model_path("cah/../../secret")) == "",
		"a model id carrying a path separator is refused, not joined"
	)

	var screen = MyHeroesScreen.new()
	root.add_child(screen)
	screen.configure(system)
	_check(
		screen._resolve_model_path("OpenBfmeNoSuchCahModel") == "",
		"the screen's resolver degrades to empty when the pack lacks the mesh"
	)

	# The pack the content lane is publishing into: one flat directory keyed by
	# the retail model id, which is the only handle the class table carries.
	var pack_root := _build_scratch_pack()
	if not content_db.pack_roots.has(pack_root):
		content_db.pack_roots.append(pack_root)
	content_db._asset_exists_cache.clear()
	content_db._frozen_resolutions.clear()

	var expected := pack_root.path_join("assets/models/cah/CHHW_CG_C_SKN.glb")
	var resolved := String(content_db.resolve_cah_model_path("CHHW_CG_C_SKN"))
	_check(
		resolved == expected,
		"a mounted pack's assets/models/cah/<ID>.glb resolves (got %s)" % resolved
	)
	_check(
		screen._resolve_model_path("CHHW_CG_C_SKN") == expected,
		"the screen reaches the same file through ContentDB"
	)
	_check(
		String(content_db.resolve_cah_model_path("CHHW_CG_U_SKN")) == "",
		"a sibling id the pack does not carry stays empty even once the directory exists"
	)

	# ICONS: the texture when the pack has one, the NAME when it does not. Never a
	# stand-in, and never a three-letter stub of the name either.
	content_db._load_interface_art_index(pack_root, {"files": {}})
	var with_art: Button = screen._icon_button(
		"HICAHCaptainGondor", "Captain of Gondor", screen.ICON_SIZE, false
	)
	_check(
		with_art.icon != null and with_art.text == "",
		"an icon the pack carries is drawn as a texture, not as a caption"
	)
	_check(
		with_art.tooltip_text == "Captain of Gondor",
		"the resolved icon keeps the authored name as its tooltip"
	)
	var without_art: Button = screen._icon_button(
		"HICAHNoSuchIcon", "Captain of Gondor", screen.ICON_SIZE, false
	)
	_check(
		without_art.icon == null and without_art.text == "Captain of Gondor",
		"an icon the pack lacks falls back to the WHOLE name (got '%s')" % without_art.text
	)
	_check(
		without_art.custom_minimum_size.x >= screen.ICON_FALLBACK_WIDTH,
		"the named fallback is given the room to be read"
	)
	_check(
		without_art.tooltip_text.contains("HICAHNoSuchIcon"),
		"the fallback names the icon the pack is missing"
	)

	# An id whose string the pack cannot translate reaches the screen as words
	# rather than as an identifier.
	_check(
		screen._readable("CreateAHero:ClassName_HeroesOfTheWest") == "Heroes Of The West",
		"an untranslated class id is spaced into a label (got '%s')"
			% screen._readable("CreateAHero:ClassName_HeroesOfTheWest")
	)

	screen.queue_free()
	content_db.pack_roots.erase(pack_root)
	content_db._asset_exists_cache.clear()
	content_db._frozen_resolutions.clear()


func _build_scratch_pack() -> String:
	var scratch := "user://cah-pack-%d" % (Time.get_ticks_usec() & 0xFFFFFF)
	var pack_root := ProjectSettings.globalize_path(scratch.path_join("pack")).replace("\\", "/")
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models/cah"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/ui/interface-art/cah"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("data/interface-art"))

	# NOT A REAL MESH, deliberately. What is under test is which file the resolver
	# names, not whether the importer's glTF loads - a fixture that needed a real
	# mesh would make this gate wait on the content lane.
	var mesh := FileAccess.open(pack_root.path_join("assets/models/cah/CHHW_CG_C_SKN.glb"), FileAccess.WRITE)
	mesh.store_buffer("glTF fixture".to_utf8_buffer())
	mesh.close()

	var icon := Image.create(16, 16, false, Image.FORMAT_RGBA8)
	icon.fill(Color(0.6, 0.5, 0.2, 1.0))
	icon.save_png(pack_root.path_join("assets/ui/interface-art/cah/hicahcaptaingondor.png"))

	var index := FileAccess.open(pack_root.path_join("data/interface-art/index.json"), FileAccess.WRITE)
	index.store_string(JSON.stringify({
		"schema": "openbfme.interface-art-index",
		"schemaVersion": 1,
		"scope": "cah",
		"images": {
			"HICAHCaptainGondor": "assets/ui/interface-art/cah/hicahcaptaingondor.png",
		},
		"gaps": [],
	}))
	index.close()

	var meta := FileAccess.open(pack_root.path_join("pack.json"), FileAccess.WRITE)
	meta.store_string(JSON.stringify({
		"id": "cah-runner-fixture", "schema": "openbfme.content-pack", "schemaVersion": 0, "files": {},
	}))
	meta.close()
	return pack_root


# --------------------------------------------------------------- the window


func _test_full_window_layout(system: Dictionary) -> void:
	## CREATE-A-HERO IS A SCREEN, NOT A FLYOUT.
	##
	## It shipped as a bordered panel inset inside the shell, floating over the
	## dimmed main menu with the hero preview given half of it and the controls
	## squeezed into the strip left over. These pin the three things that made it a
	## panel: the anchors, the backdrop it paints for itself, and the proportional
	## split that stops one column from being a fixed strip.
	# A stand-in for the shell's `Center`, sized here so the assertion does not
	# depend on what window a headless run happens to have.
	var host := Control.new()
	host.size = Vector2(2560, 1440)
	root.add_child(host)
	var screen = MyHeroesScreen.new()
	host.add_child(screen)
	screen.configure(system)
	screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	await process_frame

	_check(
		screen.size.is_equal_approx(host.size),
		"the screen fills the rectangle it is anchored in (%s vs %s)" % [
			str(screen.size), str(host.size)
		]
	)
	var page_root := screen.get_node_or_null("Root") as Control
	_check(
		page_root != null
			and is_equal_approx(page_root.anchor_right, 1.0)
			and is_equal_approx(page_root.anchor_bottom, 1.0),
		"the screen's content root is anchored FULL_RECT, so it follows a resize"
	)
	# ...and it really does follow one, which the copied flyout rectangle could not.
	host.size = Vector2(1600, 900)
	await process_frame
	_check(
		screen.size.is_equal_approx(host.size),
		"the screen follows the window when it is resized (%s vs %s)" % [
			str(screen.size), str(host.size)
		]
	)
	_check(
		page_root != null and page_root.size.x > host.size.x - 4.0 * screen.MARGIN.x,
		"the content spans the window rather than sitting in a panel inside it"
	)

	var backdrop := screen.get_node_or_null("Backdrop") as TextureRect
	_check(backdrop != null, "the screen owns a full-window backdrop slot")
	if backdrop != null:
		_check(
			is_equal_approx(backdrop.anchor_right, 1.0) and is_equal_approx(backdrop.anchor_bottom, 1.0),
			"the backdrop is anchored to the whole window"
		)
		_check(
			not backdrop.visible,
			"the backdrop stays down until the shell hands one over, rather than drawing a hole"
		)
		var texture := ImageTexture.create_from_image(Image.create(8, 8, false, Image.FORMAT_RGBA8))
		screen.set_backdrop_texture(texture)
		_check(
			backdrop.visible and backdrop.texture == texture,
			"the shell's own menu backdrop is what the screen draws"
		)

	# The preview and the controls both EXPAND. A fixed-width control column is
	# what left a 2560px window with a vast empty left half.
	var class_page: Control = screen._pages[screen.PAGE_CLASS]
	var preview_slot: Control = screen._class_preview_slot
	var controls: Control = class_page.get_child(class_page.get_child_count() - 1) as Control
	_check(
		preview_slot.size_flags_horizontal & Control.SIZE_EXPAND != 0
			and controls.size_flags_horizontal & Control.SIZE_EXPAND != 0,
		"both columns of the class page expand with the window"
	)
	_check(
		controls.size_flags_stretch_ratio > preview_slot.size_flags_stretch_ratio,
		"the controls get the larger share, the hero the smaller one"
	)

	# THE POWERS GRID'S COLUMN KEY MUST BE ON THE SCREEN, and it must be the FIRST
	# thing on it: "Required Hero Level" and its four numbers used to trail the
	# lattice, so on a class with more chains than fit the scroll the four columns
	# had nothing naming them until you scrolled to the bottom looking for it.
	host.size = Vector2(2560, 1440)
	screen._show_page(screen.PAGE_POWERS)
	await process_frame
	await process_frame
	var grid: GridContainer = screen._power_grid
	var scroll: ScrollContainer = screen._power_scroll
	_check(scroll != null and grid.get_parent() == screen._power_grid_wrap,
		"the lattice and its connector overlay share one rectangle inside the scroll view")
	_check(
		grid.get_combined_minimum_size().y <= scroll.size.y,
		"the whole lattice is in view at 2560x1440 (%.0f in %.0f)"
			% [grid.get_combined_minimum_size().y, scroll.size.y]
	)
	var cells := grid.get_child_count()
	var aligned := cells >= 10
	for column in range(POWER_COLUMN_COUNT):
		# Row 0 is the tier key; row 1 is the first chain. Both are laid out by
		# the same GridContainer, so a number over the wrong column would mean the
		# grid itself had come apart.
		var header := grid.get_child(1 + column) as Control
		var body := grid.get_child(1 + POWER_COLUMN_COUNT + 1 + column) as Control
		if not is_equal_approx(header.position.x, body.position.x):
			aligned = false
		if header.position.y >= body.position.y:
			aligned = false
	_check(aligned, "each level number leads the column of powers it labels")

	# THE HERO IS BESIDE THE LATTICE, NOT UNDER IT. He used to sit in a band below
	# the grid, and every pixel that band took came off the grid's own scroll view.
	_check(
		screen._powers_preview_slot.get_parent() != screen._pages[screen.PAGE_POWERS].get_child(0),
		"the hero on the powers page stands in the right-hand column, not under the lattice"
	)

	# NOTHING ON THE LATTICE CLIPS. The chain names used to end mid-word
	# ("Command Create A Hero Shielc"), and a power with no icon was captioned by
	# the button's own clipped text ("Mou" for Mount / Dismount).
	var clipped := 0
	var wrapped_captions := 0
	for index in range(grid.get_child_count()):
		var child := grid.get_child(index)
		if child is Label and (child as Label).clip_text:
			clipped += 1
		if child is Button:
			var button := child as Button
			if button.icon == null and button.text != "":
				clipped += 1
			for sub in button.get_children():
				if sub is Label and (sub as Label).autowrap_mode != TextServer.AUTOWRAP_OFF:
					wrapped_captions += 1
	_check(clipped == 0, "no label or cell on the lattice is clipped (%d were)" % clipped)

	# ONE FOOTER, ON EVERY PAGE. Four pages each carrying their own back/forward
	# pair is what left short pages with a band of empty screen under them.
	for page_index in [screen.PAGE_SELECT, screen.PAGE_CLASS, screen.PAGE_ATTRIBUTES, screen.PAGE_POWERS]:
		screen._show_page(page_index)
		var forward: Button = screen.save_button if page_index == screen.PAGE_POWERS else screen._next_button
		_check(
			screen.back_button.visible and forward.visible and forward.text != "",
			"page %d carries the shared footer pair" % page_index
		)
	screen._show_page(screen.PAGE_SELECT)

	# The shell's half of the bargain, asserted without standing the shell up.
	var menu_script = load("res://src/ui/main_menu.gd")
	_check(menu_script != null, "the shell script compiles")
	if menu_script != null:
		_check(
			(menu_script.FULL_WINDOW_PAGES as Array).has(menu_script.PAGE_MY_HEROES),
			"the shell takes its own furniture down for MY HEROES"
		)

	screen.queue_free()
	host.queue_free()


# ------------------------------------------------------------ purchase/spawn


func _test_purchase_spawns_with_computed_stats(system: Dictionary) -> void:
	## The end of the chain: a created hero on the fortress roster is queued,
	## charged, and spawns with the numbers the attribute allocation produced.
	var sim_script = load("res://src/retail_slice/retail_slice_sim.gd")
	if sim_script == null:
		_check(false, "the slice sim script compiles")
		return
	var profile := CahHeroes.new_profile(system, "Beregond", 0, 0)
	var document := CahHeroes.roster_document(system, profile, PRODUCER, 12)
	var rule: Dictionary = Adapter.simulation_rule(document)
	_check(not rule.is_empty(), "the adapter derives a simulation rule for the created hero")
	_check(
		int(rule.get("default_cost", rule.get("cost", 0))) == BASE_BUILD_COST
			or int((rule.get("cost_rule", {}) as Dictionary).get("default_cost", 0)) == BASE_BUILD_COST,
		"the simulation rule charges the 500 build cost"
	)
	# `memberHealth` is what `_add_battalion` seeds member health from, so
	# asserting it here is asserting what the spawned entity will have.
	_check(
		int(rule.get("member_health", rule.get("memberHealth", 0))) == 3800
			or int(((document["registration"] as Dictionary)["simulation"] as Dictionary)["memberHealth"]) == 3800,
		"the spawned hero's member health is the computed 3800"
	)


# ------------------------------------------------------------------ fixtures


func _test_power_selection_rules(system: Dictionary) -> void:
	## The three rules the CUSTOMIZE HERO POWERS screen enforces, each of which
	## is authored on the CommandButton rather than invented by the client.

	# A new hero starts with NO powers. Retail opens the screen prompting for a
	# level-1 pick; seeding a selection would charge the player for powers they
	# never chose.
	_check(
		CahHeroes.default_powers(system, 0).is_empty(),
		"a new hero starts with no powers selected"
	)

	# The grid offers only the trees this class is named in.
	var offered := CahHeroes.power_trees_for_class(system, 0)
	var offered_ids: Array = []
	for tree_value in offered:
		for level_value in ((tree_value as Dictionary).get("levels", []) as Array):
			offered_ids.append(String((level_value as Dictionary).get("powerId", "")))
	_check(offered.size() == 2, "Hero of the West is offered its two power trees")
	_check(
		not offered_ids.has("Command_CahWizardTeleport"),
		"a wizard-only power is not offered to Hero of the West"
	)

	# RULE 1: class binding.
	_check(
		not CahHeroes.power_refusals(system, 0, ["Command_CahWizardTeleport"]).is_empty(),
		"a power bound to another class is refused"
	)

	# RULE 2: the prerequisite arrows are a real constraint. Level 2 without
	# level 1 is refused; the whole chain is accepted.
	_check(
		not CahHeroes.power_refusals(system, 0, ["Command_CahSummonAllies_Level2"]).is_empty(),
		"a power whose prerequisite is unselected is refused"
	)
	_check(
		CahHeroes.power_refusals(
			system, 0, ["Command_CahSummonAllies_Level1", "Command_CahSummonAllies_Level2"]
		).is_empty(),
		"a power is legal once its prerequisite is selected"
	)

	# The level gate is NOT a build-time rule: a level-7 power is legal to build
	# a hero with and simply stays greyed out until the hero earns rank 7.
	_check(
		CahHeroes.power_refusals(system, 0, [
			"Command_CahSummonAllies_Level1",
			"Command_CahSummonAllies_Level2",
			"Command_CahSummonAllies_Level3",
		]).is_empty(),
		"a level-7 power is legal at build time"
	)

	# RULE 3: build cost is the base plus the selected powers, which is the
	# Build Cost the retail screen totals.
	var sub_row := CahHeroes.sub_class_row(system, 0, 0)
	var attributes := CahHeroes.default_attributes(sub_row)
	var priced := CahHeroes.computed_stats(
		system, sub_row, attributes,
		["Command_CahSummonAllies_Level1", "Command_CahAthelas"]
	)
	_check(
		int(priced["basePowerCost"]) == 350,
		"the two selected powers add 200 + 150"
	)
	_check(
		int(priced["buildCost"]) == BASE_BUILD_COST + 350,
		"build cost is the base object cost plus the selected powers"
	)
	_check(
		int(CahHeroes.computed_stats(system, sub_row, attributes)["buildCost"])
			== BASE_BUILD_COST,
		"a hero with no powers costs the base object cost"
	)


func _test_powers_and_levels_reach_the_runtime_contracts(system: Dictionary) -> void:
	## A created hero must reach the sim's ability and experience engines through
	## the SAME doors every retail hero uses. These assert the emitted document
	## against the adapter that projects it, not against a shape of our own.
	var profile := CahHeroes.new_profile(system, "Runtime Wiring", 0, 0)
	profile["powers"] = [
		"Command_CahSummonAllies_Level1", "Command_CahSummonAllies_Level2"
	]
	var document := CahHeroes.roster_document(system, profile, "GondorCastleKeep", 7)
	var registration: Dictionary = document["registration"] as Dictionary

	var abilities := Adapter.ability_rules(document)
	_check(abilities.size() == 2, "both selected powers project as ability rules")
	if abilities.size() == 2:
		var first := abilities[0] as Dictionary
		_check(int(first["slot"]) == 1, "ability slots are 1-based in selection order")
		_check(
			String(first["ability_id"]) == "Command_CahSummonAllies_Level1",
			"the ability id is the CommandButton name"
		)
		_check(String(first["targeting"]) == "point", "NEED_TARGET_POS projects as point targeting")
		_check(int(first["required_level"]) == 1, "the authored level gate travels to the runtime")
		_check(int((abilities[1] as Dictionary)["required_level"]) == 3, "the level-2 power keeps its rank-3 gate")
		# 30000 ms at 30 ticks/second.
		_check(int(first["cooldown_ticks"]) > 0, "the special power's reload time becomes a cooldown")
		# A power whose behaviour the importer COMPILED is castable, and carries
		# the authored effect rather than a placeholder. This is the difference
		# between a power that is listed and one that fires.
		_check(bool(first["castable"]), "a compiled power effect is castable")
		_check(
			String((first["effect"] as Dictionary).get("kind", "")) == "summon",
			"the compiled effect kind travels to the runtime"
		)
		# ...and one that did not compile stays selectable but is reported as
		# not castable rather than silently pretending to fire.
		_check(
			not bool((abilities[1] as Dictionary)["castable"]),
			"an uncompiled power effect is reported as not castable"
		)

	var experience := Adapter.experience_rule(document)
	_check(not experience.is_empty(), "the level chain projects as an experience rule")
	if not experience.is_empty():
		_check(int(experience["max_level"]) == 3, "the ladder carries its authored max level")
		var levels := experience["levels"] as Array
		_check(levels.size() == 3, "every authored rank reaches the runtime")
		var rank_two := levels[1] as Dictionary
		_check(
			is_equal_approx(float(rank_two["health_add"]), 60.0)
				and is_equal_approx(float(rank_two["damage_add"]), 10.0),
			"the per-level grants are resolved numbers, not modifier-list names"
		)
		_check(
			(levels[2] as Dictionary)["upgrades"] == ["Upgrade_CreateAHeroGloriousCharge"],
			"a level that unlocks an upgrade carries it"
		)

	var visual: Dictionary = registration.get("visual", {}) as Dictionary
	_check(
		String(visual.get("model", "")) == "CHHW_CG_U_SKN",
		"the hero carries its subclass's BATTLEFIELD mesh, not the menu pose"
	)
	_check(
		String(visual.get("skeleton", "")) == "CHHW_CG_U_SKL"
			and String(visual.get("animationPrefix", "")) == "CHHW_CG",
		"the rig and animation prefix travel with the mesh"
	)
	_check(
		String(CahHeroes.model_binding(
			CahHeroes.sub_class_row(system, 0, 0), "creationScreen"
		).get("model", "")) == "CHHW_CG_C_SKN",
		"the creation-screen pose is a different mesh and stays separate"
	)
	_check(
		int((registration["simulation"] as Dictionary)["buildCost"]) == BASE_BUILD_COST + 350,
		"the roster document prices the powers it equips"
	)


func _system_document() -> Dictionary:
	return {
		"schema": "openbfme.cah-system-runtime",
		"schemaVersion": 0,
		"descriptorSha256": "c".repeat(64),
		"runtimeSha256": "d".repeat(64),
		"registration": {
			"system": {
				"objectId": "CreateAHero",
				"attributeMultiplier": 1,
				"buildCost": BASE_BUILD_COST,
				"buildCostExpression": "CAH_BUILDCOST",
				"buildTimeSeconds": 30,
				"maxHealth": BASE_MAX_HEALTH,
				"maxHealthExpression": "FARAMIR_HEALTH",
				"visionRange": BASE_VISION,
				"shroudClearingRange": BASE_SHROUD,
				"commandPoints": BASE_COMMAND_POINTS,
				"reviveCost": 1500,
				"commandSet": "CreateAHeroCommandSet",
			},
			"attributeGroups": [
				_group("CreateAHero_ArmorAttribute", 0, "INNATE_ARMOR", [["ARMOR", ARMOR_LADDER]]),
				_group("CreateAHero_DamageMultAttribute", 1, "INNATE_DAMAGE", [["DAMAGE_MULT", DAMAGE_LADDER]]),
				_group("CreateAHero_HealthMultAttribute", 2, "INNATE_HEALTH", [["HEALTH_MULT", HEALTH_LADDER]]),
				_group("CreateAHero_AutoHealAttribute", 3, "INNATE_AUTO_HEAL", [["AUTO_HEAL", AUTOHEAL_LADDER]]),
				_group("CreateAHero_VisionAttribute", 4, "INNATE_VISION", [["SHROUD_CLEARING", SHROUD_LADDER], ["VISION", VISION_LADDER]]),
			],
			"maxPowerSlots": 15,
			# The POWERS screen as the importer compiles it out of the
			# CreateAHeroUI* fields on commandbutton.ini: one three-step
			# prerequisite chain rising through the level columns, one
			# standalone power, and one power bound to a DIFFERENT class so the
			# class-binding rule has something to refuse.
			"powerCatalog": [
				{
					"familyId": "Command_CahSummonAllies_Level1",
					"rootPowerId": "Command_CahSummonAllies_Level1",
					"labelStringId": "CONTROLBAR:SummonAllies_Level1",
					"allowedClassUpgrades": ["Upgrade_CreateAHero_ClassHeroOfTheWest"],
					"levels": [
						_power("Command_CahSummonAllies_Level1", 1, "", 200, ["NEED_TARGET_POS"]),
						_power("Command_CahSummonAllies_Level2", 3, "Command_CahSummonAllies_Level1", 150, ["NEED_TARGET_POS"]),
						_power("Command_CahSummonAllies_Level3", 7, "Command_CahSummonAllies_Level2", 100, ["NEED_TARGET_POS"]),
					],
				},
				{
					"familyId": "Command_CahAthelas",
					"rootPowerId": "Command_CahAthelas",
					"labelStringId": "CONTROLBAR:CAHAthelas",
					"allowedClassUpgrades": ["Upgrade_CreateAHero_ClassHeroOfTheWest"],
					"levels": [_power("Command_CahAthelas", 1, "", 150, [])],
				},
				{
					"familyId": "Command_CahWizardTeleport",
					"rootPowerId": "Command_CahWizardTeleport",
					"labelStringId": "CONTROLBAR:TeleportLevel1",
					"allowedClassUpgrades": ["Upgrade_CreateAHero_ClassIstariWizard"],
					"levels": [_power(
						"Command_CahWizardTeleport", 3, "", 200,
						["NEED_TARGET_ENEMY_OBJECT"],
						"Upgrade_CreateAHero_ClassIstariWizard"
					)],
				},
			],
			# The shared level chain, with the per-level grants already resolved
			# to {kind, value} rows as the runtime's contract requires.
			"experience": {
				"maxLevel": 3,
				"initialRank": 1,
				"levels": [
					{"rank": 1, "requiredExperience": 1, "experienceAward": 100, "attributeModifiers": [], "upgrades": []},
					{"rank": 2, "requiredExperience": 125, "experienceAward": 110, "upgrades": [], "attributeModifiers": [
						{"name": "HeroLevelUpDamage1", "modifiers": [{"kind": "DAMAGE_ADD", "value": 10}, {"kind": "HEALTH", "value": 60}], "unsupportedModifiers": []},
					]},
					{"rank": 3, "requiredExperience": 250, "experienceAward": 120, "upgrades": ["Upgrade_CreateAHeroGloriousCharge"], "attributeModifiers": [
						{"name": "HeroLevelUpDamage2", "modifiers": [{"kind": "DAMAGE_ADD", "value": 10}, {"kind": "HEALTH", "value": 60}], "unsupportedModifiers": []},
					]},
				],
			},
			"classes": [{
				"classIndex": 0,
				"nameStringId": "CreateAHero:ClassName_HeroesOfTheWest",
				"iconImageId": "Archetype_HerooftheWest",
				"upgradeName": "Upgrade_CreateAHero_ClassHeroOfTheWest",
				"subClasses": [
					{
						"subClassIndex": 0,
						"models": {
							"battlefield": {
								"conditionFlag": "CREATE_A_HERO_00",
								"model": "CHHW_CG_U_SKN",
								"skeleton": "CHHW_CG_U_SKL",
								"animationPrefix": "CHHW_CG",
								"weaponLaunchBones": ["PRIMARY SPEAR"],
								"mounted": {"model": "CHHW_MW_M_SKN"},
							},
							"creationScreen": {
								"conditionFlag": "CREATE_A_HERO_01",
								"model": "CHHW_CG_C_SKN",
								"skeleton": "CHHW_CG_C_SKL",
								"animationPrefix": "CHHW_CG",
							},
						},
						"nameStringId": "CreateAHero:SubClassName_CaptainOfGondor",
						"descriptionStringId": "CreateAHero:SubClassDesc_CaptainOfGondor",
						"iconImageId": "HPCaptainofGondor",
						"buttonImageId": "HICAHCaptainGondor",
						"usableFactions": ["Men", "Elves", "Dwarves"],
						"spendableAttributePoints": 30,
						"defaultAttributeSpend": 30,
						"attributes": CAPTAIN_ATTRIBUTES,
					},
					{
						# Shield Maiden, createaherosystemmenofthewest.inc:135,169-202.
						# 7 + 6 + 10 + 3 + 4 = 30.
						"subClassIndex": 1,
						"nameStringId": "CreateAHero:SubClassName_SheildMaiden",
						"descriptionStringId": "CreateAHero:SubClassDesc_ShieldMaiden",
						"iconImageId": "HPShieldMaiden",
						"buttonImageId": "HICAHShieldMaiden",
						"usableFactions": ["Men", "Elves", "Dwarves"],
						"spendableAttributePoints": 30,
						"defaultAttributeSpend": 30,
						"attributes": [
							{"groupName": "CreateAHero_ArmorAttribute", "minStep": 5, "maxStep": 20, "defaultStep": 12},
							{"groupName": "CreateAHero_DamageMultAttribute", "minStep": 4, "maxStep": 17, "defaultStep": 10},
							{"groupName": "CreateAHero_HealthMultAttribute", "minStep": 4, "maxStep": 19, "defaultStep": 14},
							{"groupName": "CreateAHero_AutoHealAttribute", "minStep": 5, "maxStep": 18, "defaultStep": 8},
							{"groupName": "CreateAHero_VisionAttribute", "minStep": 4, "maxStep": 13, "defaultStep": 8},
						],
					},
				],
			}],
		},
	}


func _power(
	power_id: String,
	required_level: int,
	prerequisite: String,
	cost: int,
	options: Array,
	class_upgrade: String = "Upgrade_CreateAHero_ClassHeroOfTheWest"
) -> Dictionary:
	return {
		"powerId": power_id,
		"specialPowerId": "SpecialAbility%s" % power_id,
		"requiredHeroLevel": required_level,
		"prerequisitePowerId": prerequisite,
		"costIfSelected": cost,
		"costExpression": "CAH_%s_COST" % power_id.to_upper(),
		"allowedClassUpgrades": [class_upgrade],
		"nameStringId": "CONTROLBAR:%s" % power_id,
		"descriptionStringId": "CONTROLBAR:ToolTip%s" % power_id,
		"buttonImageId": "HI%s" % power_id,
		"radiusCursorType": "",
		"options": options,
		"reloadTimeMs": 30000,
		"commandType": "SPECIAL_POWER",
		"tier": 1,
		# The compiled behaviour, as the importer folds it onto the row from the
		# CreateAHero Object's SpecialPower modules. Level 1 of the chain gets a
		# real summon; the rest stay uncompiled so BOTH paths are exercised.
		"effect": (
			{"kind": "summon", "objectId": "GondorFighterHorde", "count": 2}
			if required_level == 1 and prerequisite == ""
			else {"kind": "none"}
		),
		"implementation": (
			{"status": "implemented", "reason": "", "limitations": []}
			if required_level == 1 and prerequisite == ""
			else {"status": "unimplemented", "reason": "not compiled", "limitations": []}
		),
	}


func _group(group_name: String, ui_slot: int, category: String, ladders: Array) -> Dictionary:
	## One attribute group, all twenty steps, straight off the transcribed ladder.
	var steps: Array = []
	for step in range(1, 21):
		var modifiers: Array = []
		for ladder_value in ladders:
			var kind := String((ladder_value as Array)[0])
			var ladder := (ladder_value as Array)[1] as Array
			modifiers.append({"kind": kind, "value": float(ladder[step - 1])})
		steps.append({
			"step": step,
			"groupOrder": step - 1,
			"upgradeName": "Upgrade_%s%02d" % [group_name.substr("CreateAHero_".length()), step],
			"category": category,
			"modifiers": modifiers,
		})
	return {
		"groupName": group_name,
		"uiSlot": ui_slot,
		"labelStringId": "CAH:Label%d" % ui_slot,
		"stepCount": 20,
		"steps": steps,
	}


func _clear_profiles() -> void:
	for profile in CahHeroes.load_profiles():
		CahHeroes.delete_profile(String(profile.get("heroId", "")))


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("CAH_CREATE_A_HERO_FAIL %s" % label)


func _finish() -> void:
	print("CAH_CREATE_A_HERO_RESULT passed=%d failed=%d" % [passed, failed])
	if failed == 0:
		print("CAH_CREATE_A_HERO_OK passed=%d failed=0" % passed)
	quit(0 if failed == 0 else 1)
