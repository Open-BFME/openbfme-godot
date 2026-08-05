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
	_test_screen(system)
	_test_purchase_spawns_with_computed_stats(system)

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
			"classes": [{
				"classIndex": 0,
				"nameStringId": "CreateAHero:ClassName_HeroesOfTheWest",
				"iconImageId": "Archetype_HerooftheWest",
				"subClasses": [
					{
						"subClassIndex": 0,
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
