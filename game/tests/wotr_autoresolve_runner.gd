extends SceneTree

## RETAIL'S AUTO-RESOLVE MODEL, checked against retail's own documents.
##
## WHAT THIS RUNNER IS FOR. A model of somebody else's combat system can fail
## into something that still looks like a battle: the tables load, a winner comes
## out, and every number in it is this project's. This runner separates the two
## kinds of claim and never lets one stand in for the other:
##
##   * WHAT RETAIL STATES is asserted against retail's own authored numbers and
##     retail's own comments - the census, the handicap ladder's algebra, the
##     reinforcement rounds, the rock-paper-scissors ring in the combat chains,
##     the fallback blocks, the disabled bonus tiers.
##   * WHAT RETAIL DOES NOT STATE is asserted as a PROPERTY of the model -
##     determinism, symmetry, monotonicity, termination - and never as an
##     expected winner, because retail states no expected winner anywhere.
##
## RETAIL STATES NO WORKED EXAMPLE. There is no battle in any of the nine
## documents with an outcome written next to it, no regression fixture and no
## self-check. That is a finding, not an omission here: every "retail says"
## assertion below is a TABLE VALUE or a COMMENT, and the composed outcome of a
## whole battle is checked only for properties. A test that asserted a winner
## would only be agreeing with this project's own arithmetic.
##
## IT IS A SEPARATE RUNNER ON PURPOSE, for the reason `wotr_markers_runner` and
## `wotr_region_card_runner` are separate: folding these in would move a stated
## floor that other people rely on. This runner carries its own.
##
## LIVENESS. The expected check count is asserted, so a function that aborts
## before its assertions fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/wotr_autoresolve_runner.gd
## With no bundle present it still runs its unbound checks and says so; that is
## the 8/8 mode.

const AutoResolve = preload("res://src/wotr/wotr_autoresolve.gd")

## RETAIL'S OWN CENSUS, measured off the nine shipped documents rather than
## quoted from a plan. The catalog carries NINE `livingworldautoresolve*.ini`
## files, not eleven.
const RETAIL_DOCUMENTS := 9
const RETAIL_ARMORS := 131
const RETAIL_WEAPONS := 150
const RETAIL_BODIES := 102
const RETAIL_COMBAT_CHAINS := 9
const RETAIL_LEADERSHIPS := 13
const RETAIL_HANDICAP_LEVELS := 21
const RETAIL_REINFORCEMENT_SCHEDULES := 2
const RETAIL_DEFINES := 2254
const RETAIL_ARMOR_ROWS := 1161
const RETAIL_DAMAGE_ROWS := 1500
const RETAIL_HITPOINT_ROWS := 653
const RETAIL_TARGET_ROWS := 27

## The nine unit types `livingworldautoresolvearmor.ini` says are "defined in
## code", plus the tenth key every weapon block authors with retail's comment
## "For now, since not everyone has a type yet".
const RETAIL_UNIT_TYPES := 9
const UNIT_INVALID := "AutoResolveUnit_INVALID"

## THE 18 ARMOR BLOCKS RETAIL SHIPS WITH A ROW MISSING, BY NAME. Seven fortress
## armors omit their row against `AutoResolveUnit_Fortress` and eleven Arnor
## armors omit their row against `AutoResolveUnit_Support`. They are asserted by
## NAME rather than by a count so a converter that started dropping rows cannot
## hide inside the tolerance.
const ARMORS_MISSING_VS_FORTRESS := [
	"AutoResolve_AngmarFortressArmor",
	"AutoResolve_DwarvenFortressArmor",
	"AutoResolve_ElvenFortressArmor",
	"AutoResolve_IsengardFortressArmor",
	"AutoResolve_MenFortressArmor",
	"AutoResolve_MordorFortressArmor",
	"AutoResolve_WildFortressArmor",
]
const ARMORS_MISSING_VS_SUPPORT := [
	"AutoResolve_ArnorArcherArmor",
	"AutoResolve_ArnorArcherHeavyArmor",
	"AutoResolve_ArnorKnightArmor",
	"AutoResolve_ArnorKnightHeavyArmor",
	"AutoResolve_ArnorRangerArmor",
	"AutoResolve_ArnorRohirrimArmor",
	"AutoResolve_ArnorRohirrimHeavyArmor",
	"AutoResolve_ArnorSoldierArmor",
	"AutoResolve_ArnorSoldierHeavyArmor",
	"AutoResolve_ArnorTowerGuardArmor",
	"AutoResolve_ArnorTowerGuardHeavyArmor",
]

## The seven sides `livingworldautoresolveresourcebonus.ini` lists, RotWK's seven
## playable factions including Angmar.
const RETAIL_BONUS_SIDES := [
	"PlayerMen", "PlayerElves", "PlayerDwarves", "PlayerWild",
	"PlayerMordor", "PlayerIsengard", "PlayerAngmar",
]

## LIVENESS ARITHMETIC. 8 checks need no bundle (the loader's own refusals and
## the boundary scan); the other 88 read retail's tables. 8 + 88 = 96.
const CHECKS_WITH_BUNDLE := 96
const CHECKS_WITHOUT_BUNDLE := 8

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING AUTO-RESOLVE ==")
	var model := AutoResolve.new()
	var located: Dictionary = model.locate_and_load(_roots())
	var bound := bool(located.get("ok", false))
	if bound:
		print("bundle: %s" % String(located["path"]))
	else:
		print("bundle: NONE - %s" % String(located["reason"]).split(".")[0])

	_check_the_loader_and_the_boundary(located, bound)
	if bound:
		var rules: Dictionary = model.rules
		_check_retails_census(rules)
		_check_retails_preprocessor_left_nothing_unresolved(rules)
		_check_retails_hard_coded_defaults(rules)
		_check_retails_handicap_ladder(rules)
		_check_retails_reinforcement_schedule(rules)
		_check_retails_combat_chain_ring(rules)
		_check_retails_leaderships(rules)
		_check_retails_disabled_bonus_tables(rules)
		_check_retails_own_incomplete_blocks(rules)
		_check_the_model_says_what_retail_does_not(rules)
		_check_the_model_is_deterministic_and_symmetric(rules)
		_check_the_model_is_monotone_and_terminates(rules)

	var expected := CHECKS_WITH_BUNDLE if bound else CHECKS_WITHOUT_BUNDLE
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, expected, _passed, _failed])
	if total != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			total, expected])
		quit(1)
		return
	quit(1 if _failed > 0 else 0)


func _roots() -> Array:
	var roots: Array = []
	var content := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if not content.is_empty():
		roots.append(content)
	roots.append("user://content-packs")
	return roots


# --- the loader, and the boundary this packet exists to keep --------------------


## THE BOUNDARY IS AN ASSERTION, NOT A PROMISE IN A REPORT. This packet converts
## and models retail's auto-resolve system and is explicitly NOT authorised to
## wire it into the simulation. If a later edit calls this model from the game,
## this check reddens - which is the only way a boundary survives contact.
func _check_the_loader_and_the_boundary(located: Dictionary, bound: bool) -> void:
	var reason := String(located.get("reason", ""))
	_check("a_bound_bundle_reports_no_reason_and_an_absent_one_reports_a_reason",
		(bound and reason.is_empty()) or (not bound and not reason.is_empty()))
	var refused := AutoResolve.new()
	_check("a_path_that_is_not_a_bundle_is_refused_rather_than_half_loaded",
		not refused.load_from("res://project.godot") and not refused.loaded
			and refused.rules.is_empty())
	_check("a_refusal_carries_at_least_one_stated_error", not refused.errors.is_empty(),
		", ".join(Array(refused.errors)))
	var empty := AutoResolve.new()
	var nothing: Dictionary = empty.locate_and_load([])
	if bool(nothing.get("ok", false)):
		_check("loading_with_no_roots_either_finds_the_environments_bundle_or_explains",
			empty.loaded and not empty.rules.is_empty())
	else:
		var text := String(nothing.get("reason", ""))
		_check("loading_with_no_roots_explains_itself",
			text.contains("living_world_autoresolve") and text.contains(AutoResolve.BUNDLE_ENV),
			text.split(".")[0])
	# With no rules at all every lookup returns a stated nothing, never a number.
	_check("with_no_rules_resolve_refuses_rather_than_producing_a_winner",
		not bool((AutoResolve.resolve({}, {}, {}) as Dictionary).get("ok", true)))
	_check("with_no_rules_an_armor_lookup_is_unresolved_rather_than_1_0",
		not bool((AutoResolve.armor_multiplier({}, "anything", "AutoResolveUnit_Hero")
			as Dictionary)["resolved"]))

	var callers := _files_naming_the_model(["res://src", "res://scenes"])
	_check("nothing_outside_the_tests_calls_the_auto_resolve_model",
		callers.is_empty(), "called from: %s" % ", ".join(callers))
	var self_only := _files_naming_the_model(["res://tests"])
	_check("the_model_is_reachable_from_this_runner_and_no_other",
		self_only.size() == 1 and self_only[0].ends_with("wotr_autoresolve_runner.gd"),
		"; ".join(self_only))


func _files_naming_the_model(roots: Array) -> Array[String]:
	var found: Array[String] = []
	var pending: Array[String] = []
	for root in roots:
		pending.append(String(root))
	while not pending.is_empty():
		var directory := pending.pop_back() as String
		var handle := DirAccess.open(directory)
		if handle == null:
			continue
		handle.list_dir_begin()
		var entry := handle.get_next()
		while not entry.is_empty():
			var path := directory.path_join(entry)
			if handle.current_is_dir():
				pending.append(path)
			elif entry.ends_with(".gd") and not entry.ends_with("wotr_autoresolve.gd"):
				var file := FileAccess.open(path, FileAccess.READ)
				if file != null:
					var text := file.get_as_text()
					file.close()
					if text.contains("wotr_autoresolve.gd"):
						found.append(path)
			entry = handle.get_next()
		handle.list_dir_end()
	found.sort()
	return found


# --- what retail states -------------------------------------------------------


func _check_retails_census(rules: Dictionary) -> void:
	var totals: Dictionary = rules.get("totals", {})
	for pair in [
		["documents", RETAIL_DOCUMENTS], ["armors", RETAIL_ARMORS],
		["weapons", RETAIL_WEAPONS], ["bodies", RETAIL_BODIES],
		["combatChains", RETAIL_COMBAT_CHAINS], ["leaderships", RETAIL_LEADERSHIPS],
		["handicapLevels", RETAIL_HANDICAP_LEVELS],
		["reinforcementSchedules", RETAIL_REINFORCEMENT_SCHEDULES],
		["defines", RETAIL_DEFINES], ["armorRows", RETAIL_ARMOR_ROWS],
		["damageRows", RETAIL_DAMAGE_ROWS], ["hitpointRows", RETAIL_HITPOINT_ROWS],
		["targetPriorityRows", RETAIL_TARGET_ROWS],
	]:
		_check("the_bundle_carries_every_%s_retail_ships" % String(pair[0]),
			int(totals.get(String(pair[0]), -1)) == int(pair[1]),
			"%d of %d" % [int(totals.get(String(pair[0]), -1)), int(pair[1])])
	_check("retail_declares_nine_auto_resolve_unit_types",
		(rules.get("unitTypes", []) as Array).size() == RETAIL_UNIT_TYPES,
		"%d" % (rules.get("unitTypes", []) as Array).size())
	# Retail authors a tenth key in every weapon block and says why. It is not a
	# unit type; it is carried separately so the two cannot be confused.
	var default_weapon: Dictionary = (rules.get("weapons", {}) as Dictionary).get(
		AutoResolve.DEFAULT_WEAPON, {})
	_check("the_tenth_weapon_key_retail_calls_INVALID_is_carried_but_not_a_unit_type",
		(default_weapon.get("damagePerRound", {}) as Dictionary).has(UNIT_INVALID)
			and not (rules.get("unitTypes", []) as Array).has(UNIT_INVALID))


func _check_retails_preprocessor_left_nothing_unresolved(rules: Dictionary) -> void:
	var unresolved: Dictionary = rules.get("unresolved", {})
	var macros: Array = unresolved.get("macros", [])
	var names: Array[String] = []
	for entry in macros:
		names.append(String((entry as Dictionary).get("name", "?")))
	# BY NAME, not by a tolerance: if a macro stops resolving, its name is here.
	_check("every_macro_retails_documents_name_resolves",
		names.is_empty(), "unresolved: %s" % ", ".join(names))
	var references: Array = unresolved.get("references", [])
	var wanted: Array[String] = []
	for entry in references:
		var row: Dictionary = entry
		wanted.append("%s.%s -> %s" % [row.get("block", "?"), row.get("field", "?"),
			row.get("name", "?")])
	_check("no_authored_field_names_a_macro_that_did_not_resolve",
		wanted.is_empty(), "; ".join(wanted))
	var gaps: Array = rules.get("gaps", [])
	_check("the_converter_read_the_nine_documents_whole",
		gaps.is_empty(), "%d unmodelled key(s)" % gaps.size())
	# The `#DIVIDE` that a prior costing said this feature needs, EVALUATED. The
	# handicap ladder authors `#DIVIDE( 1.0, 0.95 )` and nothing else in retail's
	# auto-resolve documents divides.
	var rung := AutoResolve.handicap_for(rules, 5)
	_check("retails_inline_DIVIDE_evaluated_rather_than_being_carried_as_text",
		absf(float(rung["armorMultiplier"]) - 1.0 / 0.95) < 1e-9,
		"%.10f" % float(rung["armorMultiplier"]))


func _check_retails_hard_coded_defaults(rules: Dictionary) -> void:
	# "Keep this as AutoResolve_DefaultArmor, it is used as a fallback."
	var armors: Dictionary = rules.get("armors", {})
	var default_armor: Dictionary = (armors.get(AutoResolve.DEFAULT_ARMOR, {})
		as Dictionary).get("vs", {})
	var all_hundred := default_armor.size() == RETAIL_UNIT_TYPES
	for key in default_armor.keys():
		if float((default_armor[key] as Dictionary).get("value", -1.0)) != 100.0:
			all_hundred = false
	_check("retails_default_armor_is_100_percent_against_all_nine_unit_types", all_hundred,
		"%d rows" % default_armor.size())
	# "This is the default; all other weapons are copied from it."
	var weapons: Dictionary = rules.get("weapons", {})
	var default_weapon: Dictionary = weapons.get(AutoResolve.DEFAULT_WEAPON, {})
	var damage: Dictionary = default_weapon.get("damagePerRound", {})
	var all_fifty := damage.size() == RETAIL_UNIT_TYPES + 1
	for key in damage.keys():
		if float((damage[key] as Dictionary).get("value", -1.0)) != 50.0:
			all_fifty = false
	_check("retails_default_weapon_does_50_damage_against_every_key_it_lists", all_fifty,
		"%d rows" % damage.size())
	_check("retails_default_weapon_misses_half_the_time_and_says_so",
		float(default_weapon.get("missPercentChance", -1.0)) == 50.0)
	# The inheritance is retail's claim, and it is only true because NO OTHER
	# weapon restates it. If one ever does, this reddens.
	var restating: Array[String] = []
	for name in weapons.keys():
		if String(name) == AutoResolve.DEFAULT_WEAPON:
			continue
		if (weapons[name] as Dictionary).has("missPercentChance"):
			restating.append(String(name))
	_check("no_weapon_but_the_default_declares_MissPercentChance_so_all_150_inherit_50",
		restating.is_empty(), "restated by: %s" % ", ".join(restating))
	_check("a_weapon_that_declares_no_MissPercentChance_still_reports_retails_50",
		AutoResolve.miss_percent_chance(rules, "AutoResolve_GondorArcherWeapon") == 50.0)
	# "Hard coded name. This must always be first. It gives the default entries
	# for the other bodies" - and the only entry it gives is CanBeAttacked = Yes.
	var bodies: Dictionary = rules.get("bodies", {})
	var default_body: Dictionary = bodies.get(AutoResolve.DEFAULT_BODY, {})
	_check("retails_default_body_declares_CanBeAttacked_Yes_and_nothing_else",
		bool(default_body.get("canBeAttacked", false))
			and (default_body.get("hitpointsAtLevel", []) as Array).is_empty())
	var restating_body: Array[String] = []
	for name in bodies.keys():
		if String(name) == AutoResolve.DEFAULT_BODY:
			continue
		if (bodies[name] as Dictionary).has("canBeAttacked"):
			restating_body.append(String(name))
	_check("no_body_but_the_default_declares_CanBeAttacked_so_all_102_inherit_Yes",
		restating_body.is_empty(), "restated by: %s" % ", ".join(restating_body))
	# "Everything is defaulting to zero!"
	var default_chain: Dictionary = (rules.get("combatChains", {}) as Dictionary).get(
		AutoResolve.DEFAULT_COMBAT_CHAIN, {})
	_check("retails_default_combat_chain_declares_no_target_at_all",
		(default_chain.get("targets", {}) as Dictionary).is_empty())
	# "Heroes never die in B4ME2" - the generic hero body is the one retail marks.
	_check("retails_generic_hero_body_is_kept_in_the_army_summary_even_when_killed",
		AutoResolve.leave_in_army_summary(rules, "AutoResolve_HeroBody"))
	# NO RotWK body has an active RegenerateAtLevel row: retail wrote them and
	# commented every one out. Asserted so a converter that starts reading
	# comments as data reddens here.
	_check("retail_ships_no_active_RegenerateAtLevel_row",
		int((rules.get("totals", {}) as Dictionary).get("regenerateRows", -1)) == 0)


func _check_retails_handicap_ladder(rules: Dictionary) -> void:
	var levels: Array = rules.get("handicapLevels", [])
	# 0, 5, 10 ... 100: retail authors every multiple of five and no other rung.
	var ladder_is_retails := levels.size() == RETAIL_HANDICAP_LEVELS
	for index in levels.size():
		if int((levels[index] as Dictionary).get("guiDisplayedLevel", -1)) != index * 5:
			ladder_is_retails = false
	_check("retails_handicap_ladder_is_every_multiple_of_five_from_0_to_100",
		ladder_is_retails, "%d rungs" % levels.size())
	# RETAIL'S OWN ALGEBRA. Every rung authors ArmorMultiplier as
	# `#DIVIDE( 1.0, <that rung's WeaponMultiplier> )`. That relation is a
	# statement retail makes about its own table and it is checkable exactly.
	var algebra_holds := true
	var broken: Array[String] = []
	for entry in levels:
		var rung: Dictionary = entry
		var weapon := float(rung.get("weaponMultiplier", 0.0))
		var armor := float(rung.get("armorMultiplier", 0.0))
		if weapon <= 0.0 or absf(armor - 1.0 / weapon) > 1e-6:
			algebra_holds = false
			broken.append("%d" % int(rung.get("guiDisplayedLevel", -1)))
	_check("every_handicap_rungs_armor_is_the_reciprocal_of_its_weapon_multiplier",
		algebra_holds, "broken at level(s) %s" % ", ".join(broken))
	# Retail's comments: level 0 is "no penalty", every other rung is "A penalty"
	# on the weapon and "Also a penalty" on the armor. A penalty on armor means
	# armor multiplies damage TAKEN - which is the direction the model uses.
	var zero := AutoResolve.handicap_for(rules, 0)
	_check("retails_zero_handicap_is_no_penalty_at_all",
		float(zero["weaponMultiplier"]) == 1.0 and float(zero["armorMultiplier"]) == 1.0
			and float(zero["experienceMultiplier"]) == 1.0)
	var direction_holds := true
	for entry in levels:
		var rung: Dictionary = entry
		if int(rung.get("guiDisplayedLevel", 0)) == 0:
			continue
		if float(rung.get("weaponMultiplier", 1.0)) >= 1.0:
			direction_holds = false
		if float(rung.get("armorMultiplier", 1.0)) <= 1.0:
			direction_holds = false
	_check("every_handicapped_rung_is_a_penalty_in_both_directions_as_retail_says",
		direction_holds)
	# Retail's own comment: "normal (in RTS, we don't give an XP penalty...)".
	var xp_flat := true
	for entry in levels:
		if float((entry as Dictionary).get("experienceMultiplier", 0.0)) != 1.0:
			xp_flat = false
	_check("no_handicap_rung_gives_an_experience_penalty_as_retails_comment_says", xp_flat)
	# The worst rung is 0.01 and not 0 - retail refuses to zero the weapon.
	var worst := AutoResolve.handicap_for(rules, 100)
	_check("retails_worst_handicap_leaves_one_percent_of_the_weapon_rather_than_none",
		float(worst["weaponMultiplier"]) == 0.01 and float(worst["armorMultiplier"]) == 100.0)
	# A level between rungs takes the rung at or below it and SAYS it is inexact.
	var between := AutoResolve.handicap_for(rules, 7)
	_check("a_handicap_between_two_rungs_takes_the_lower_rung_and_says_it_is_inexact",
		int(between["guiDisplayedLevel"]) == 5 and not bool(between["exact"]))


func _check_retails_reinforcement_schedule(rules: Dictionary) -> void:
	# "Primary army is available at round zero. VERY IMPORTANT: do not change
	# this, or battles will end immediately because there are no armies
	# available at the start of battle!"
	var both_at_zero := AutoResolve.reinforcement_round(rules, "Attacker", 1) == 0 \
		and AutoResolve.reinforcement_round(rules, "Defender", 1) == 0
	_check("retails_primary_army_arrives_at_round_zero_on_both_sides", both_at_zero)
	var explicit_holds := true
	for side in ["Attacker", "Defender"]:
		for pair in [[2, 15], [3, 30], [4, 45]]:
			if AutoResolve.reinforcement_round(rules, side, int(pair[0])) != int(pair[1]):
				explicit_holds = false
	_check("retails_second_third_and_fourth_armies_arrive_at_15_30_and_45", explicit_holds)
	# "EachRemaining = +1 ; For each army after the last listed, wait 1
	# additional round before bringing on another"
	_check("retails_EachRemaining_adds_one_round_per_army_past_the_fourth",
		AutoResolve.reinforcement_round(rules, "Attacker", 5) == 46
			and AutoResolve.reinforcement_round(rules, "Attacker", 9) == 50)
	_check("a_side_retail_never_scheduled_gets_no_round_rather_than_round_zero",
		AutoResolve.reinforcement_round(rules, "Bystander", 1) == -1)
	# Retail authors the two schedules identically; if they ever diverge this
	# reddens rather than one silently standing in for the other.
	var identical := true
	for index in range(1, 10):
		if AutoResolve.reinforcement_round(rules, "Attacker", index) \
				!= AutoResolve.reinforcement_round(rules, "Defender", index):
			identical = false
	_check("retail_schedules_the_attacker_and_the_defender_identically", identical)


func _check_retails_combat_chain_ring(rules: Dictionary) -> void:
	var chains: Dictionary = rules.get("combatChains", {})
	_check("retail_ships_nine_combat_chains_and_the_bundle_carries_all_nine",
		chains.size() == RETAIL_COMBAT_CHAINS, "%d" % chains.size())
	# THE RING, exactly as retail authors it: each of the four line types
	# prefers one type at +50 and refuses another at -50, and the four
	# preferences form a closed cycle.
	var ring := [
		["AutoResolve_ArcherCombatChain", "AutoResolveUnit_Soldier", "AutoResolveUnit_Cavalry"],
		["AutoResolve_SoldierCombatChain", "AutoResolveUnit_Pikemen", "AutoResolveUnit_Archer"],
		["AutoResolve_PikemenCombatChain", "AutoResolveUnit_Cavalry", "AutoResolveUnit_Soldier"],
		["AutoResolve_CavalryCombatChain", "AutoResolveUnit_Archer", "AutoResolveUnit_Pikemen"],
	]
	var ring_holds := true
	var broken: Array[String] = []
	for row in ring:
		var chain := String(row[0])
		if AutoResolve.target_priority(rules, chain, String(row[1])) != 50.0:
			ring_holds = false
			broken.append("%s likes %s" % [chain, row[1]])
		if AutoResolve.target_priority(rules, chain, String(row[2])) != -50.0:
			ring_holds = false
			broken.append("%s dislikes %s" % [chain, row[2]])
	_check("retails_four_line_chains_form_the_ring_retail_authored_at_plus_and_minus_50",
		ring_holds, "; ".join(broken))
	# Retail gives the five melee/ranged/hero chains the SAME two trailing lines -
	# a hero is worth at least 25 and a fortress exactly 15 - and then breaks that
	# pattern in exactly one place: the siege chain values a fortress at 50, above
	# every other chain. The support chain lists no fortress at all. All three
	# statements are asserted together so a converter cannot satisfy one by
	# breaking another.
	var trailing_lines := true
	var odd_chains: Array[String] = []
	for name in ["AutoResolve_ArcherCombatChain", "AutoResolve_SoldierCombatChain",
			"AutoResolve_PikemenCombatChain", "AutoResolve_CavalryCombatChain",
			"AutoResolve_HeroCombatChain"]:
		if AutoResolve.target_priority(rules, String(name), "AutoResolveUnit_Fortress") != 15.0 \
				or AutoResolve.target_priority(rules, String(name), "AutoResolveUnit_Hero") < 25.0:
			trailing_lines = false
			odd_chains.append(String(name))
	var siege := AutoResolve.target_priority(rules, "AutoResolve_SiegeCombatChain",
		"AutoResolveUnit_Fortress")
	if siege != 50.0:
		trailing_lines = false
		odd_chains.append("AutoResolve_SiegeCombatChain fortress %.0f" % siege)
	for name in chains.keys():
		if String(name) == "AutoResolve_SiegeCombatChain":
			continue
		if AutoResolve.target_priority(rules, String(name), "AutoResolveUnit_Fortress") >= siege:
			trailing_lines = false
			odd_chains.append(String(name))
	if not ((chains.get("AutoResolve_SupportCombatChain", {}) as Dictionary).get("targets", {})
			as Dictionary).is_empty():
		if (((chains["AutoResolve_SupportCombatChain"] as Dictionary)["targets"] as Dictionary)
				.has("AutoResolveUnit_Fortress")):
			trailing_lines = false
			odd_chains.append("AutoResolve_SupportCombatChain lists a fortress")
	_check("retails_five_line_chains_share_their_hero_and_fortress_lines_and_only_siege_prefers_a_fortress",
		trailing_lines, "; ".join(odd_chains))
	# "Nothing" - the monster chain, which retail's own armor header also calls
	# an unused type.
	_check("retails_monster_chain_targets_nothing_at_all",
		((chains.get("AutoResolve_MonsterCombatChain", {}) as Dictionary).get("targets", {})
			as Dictionary).is_empty())
	# "Support ----- (versatile, yet weak)": five targets, strictly descending
	# 40, 39, 38, 37, 36. The ORDER is the rule; the numbers are retail's.
	var support := [
		["AutoResolveUnit_Soldier", 40.0], ["AutoResolveUnit_Cavalry", 39.0],
		["AutoResolveUnit_Archer", 38.0], ["AutoResolveUnit_Pikemen", 37.0],
		["AutoResolveUnit_Siege", 36.0],
	]
	var descending := true
	var previous := 1e30
	for row in support:
		var value := AutoResolve.target_priority(rules, "AutoResolve_SupportCombatChain",
			String(row[0]))
		if value != float(row[1]) or value >= previous:
			descending = false
		previous = value
	_check("retails_support_chain_ranks_five_targets_strictly_downward_from_40_to_36",
		descending)
	# An unlisted type is worth zero - retail: "Zero is the default if a type is
	# not specified."
	_check("a_type_a_chain_does_not_list_is_worth_zero_rather_than_being_unreachable",
		AutoResolve.target_priority(rules, "AutoResolve_ArcherCombatChain",
			"AutoResolveUnit_Support") == 0.0)
	# A chain retail never declared falls back to the default, which targets
	# nothing - it does not silently borrow another chain's preferences.
	_check("a_chain_retail_never_declared_falls_back_to_the_default_and_not_to_a_lookalike",
		AutoResolve.target_priority(rules, "AutoResolve_ArcherCombatChainX",
			"AutoResolveUnit_Soldier") == 0.0)


func _check_retails_leaderships(rules: Dictionary) -> void:
	var leaderships: Dictionary = rules.get("leaderships", {})
	_check("retail_ships_thirteen_auto_resolve_leaderships",
		leaderships.size() == RETAIL_LEADERSHIPS, "%d" % leaderships.size())
	# RETAIL AUTHORS ALL THIRTEEN IDENTICALLY except for MinLevel: 150% weapon,
	# 50% armor, 200% experience, at most 2 units, priority 35, affecting the
	# same four line types and never heroes or siege. That uniformity is retail's
	# and is asserted, so a converter that started mixing blocks up reddens.
	var uniform := true
	var odd: Array[String] = []
	for name in leaderships.keys():
		var block: Dictionary = leaderships[name]
		var affects: Array = block.get("affects", [])
		if affects.size() != 4 or affects.has("AutoResolveUnit_Hero") \
				or affects.has("AutoResolveUnit_Siege"):
			uniform = false
			odd.append(String(name))
			continue
		if not bool(block.get("affectsHigherLevelFirst", false)):
			uniform = false
			odd.append(String(name))
			continue
		var ladder: Array = block.get("bonusForLevel", [])
		if ladder.size() != 1:
			uniform = false
			odd.append(String(name))
			continue
		var bonus: Dictionary = ladder[0]
		if float(bonus.get("weaponMultiplier", 0.0)) != 1.5 \
				or float(bonus.get("armorMultiplier", 0.0)) != 0.5 \
				or float(bonus.get("experienceMultiplier", 0.0)) != 2.0 \
				or int(bonus.get("maximumUnitsAffected", 0)) != 2 \
				or float(bonus.get("priority", 0.0)) != 35.0:
			uniform = false
			odd.append(String(name))
	_check("every_leadership_retail_ships_carries_the_same_150_50_200_bonus_for_two_units",
		uniform, "differs: %s" % ", ".join(odd))
	# "The BonusForLevel block with the highest MinLevel <= unit's current level
	# is used" - Aragorn's is MinLevel 4, so a level-3 Aragorn leads nobody.
	_check("a_leader_below_its_own_MinLevel_grants_no_bonus_at_all",
		(AutoResolve.leadership_bonus(rules, "AutoResolve_AragornBonus", 3) as Dictionary)
			.is_empty()
			and not (AutoResolve.leadership_bonus(rules, "AutoResolve_AragornBonus", 4)
				as Dictionary).is_empty())
	# "the unit is never affected by its own leadership". THE FIXTURE MATTERS: a
	# leader whose unit type its own Affects list EXCLUDES would pass this check
	# for the wrong reason - the type filter would be doing the work and the
	# self-exclusion rule could be deleted without the check noticing. Retail's
	# thirteen leaderships all affect Soldier, so the leader here is given the
	# highest level of a Soldier stack: only the self-exclusion rule keeps it out.
	var self_army := [
		_unit("Captain", "AutoResolveUnit_Soldier", 9, "AutoResolve_AragornBonus"),
		_unit("SoldierA", "AutoResolveUnit_Soldier", 3, ""),
		_unit("SoldierB", "AutoResolveUnit_Soldier", 5, ""),
	]
	var self_assigned := AutoResolve.assign_leadership(rules, self_army)
	_check("a_leader_never_gives_its_own_bonus_to_itself_even_when_its_own_type_is_affected",
		not self_assigned.has(0) and self_assigned.has(1) and self_assigned.has(2),
		"assigned to %s" % str(self_assigned.keys()))
	var army := [
		_unit("Aragorn", "AutoResolveUnit_Hero", 6, "AutoResolve_AragornBonus"),
		_unit("SoldierA", "AutoResolveUnit_Soldier", 3, ""),
		_unit("SoldierB", "AutoResolveUnit_Soldier", 5, ""),
		_unit("SoldierC", "AutoResolveUnit_Soldier", 1, ""),
	]
	var assigned := AutoResolve.assign_leadership(rules, army)
	# MaximumUnitsAffected = 2, and AffectsHigherLevelFirst = Yes picks the two
	# highest-level eligible units.
	_check("MaximumUnitsAffected_caps_the_bonus_at_retails_own_two_units",
		assigned.size() == 2, "%d units" % assigned.size())
	_check("AffectsHigherLevelFirst_gives_the_bonus_to_the_two_highest_level_units",
		assigned.has(2) and assigned.has(1) and not assigned.has(3))
	# The leader is a Hero and its own Affects list excludes heroes, so a second
	# hero gets nothing either.
	var heroes := [
		_unit("Aragorn", "AutoResolveUnit_Hero", 6, "AutoResolve_AragornBonus"),
		_unit("Faramir", "AutoResolveUnit_Hero", 6, ""),
	]
	_check("a_leadership_that_excludes_heroes_gives_a_second_hero_nothing",
		(AutoResolve.assign_leadership(rules, heroes) as Dictionary).is_empty())


func _check_retails_disabled_bonus_tables(rules: Dictionary) -> void:
	# "Disabled effect of resourse bonus on auto resolve" / "Disabled effect of
	# power points on auto resolve": retail ships every non-1.0 tier COMMENTED
	# OUT, so both tables are no-ops in RotWK. That is a fact about the shipped
	# data and it is asserted rather than assumed - if a converter ever started
	# reading commented tiers as data, this reddens.
	for section in ["resourceBonus", "sciencePurchasePointBonus"]:
		var rules_list: Array = rules.get(section, [])
		var only_the_neutral_tier := rules_list.size() == 1
		if only_the_neutral_tier:
			var bonuses: Array = (rules_list[0] as Dictionary).get("bonuses", [])
			only_the_neutral_tier = bonuses.size() == 1
			if only_the_neutral_tier:
				var bonus: Dictionary = bonuses[0]
				only_the_neutral_tier = float(bonus.get("minimum", -1.0)) == 0.0 \
					and float(bonus.get("weaponMultiplier", 0.0)) == 1.0 \
					and float(bonus.get("armorMultiplier", 0.0)) == 1.0
		_check("retails_%s_ships_one_neutral_tier_with_every_other_commented_out" % section,
			only_the_neutral_tier)
		var sides: Array = (rules_list[0] as Dictionary).get("sides", []) if not \
			rules_list.is_empty() else []
		var all_seven := sides.size() == RETAIL_BONUS_SIDES.size()
		for side in RETAIL_BONUS_SIDES:
			if not sides.has(side):
				all_seven = false
		_check("retails_%s_lists_all_seven_playable_sides_including_Angmar" % section, all_seven)
		# Whatever the amount, the shipped table gives 1.0 - the disabled tiers
		# are not reachable by any input.
		var huge := AutoResolve.threshold_bonus(rules, section, "PlayerMen", 1000.0)
		_check("no_amount_of_%s_changes_a_multiplier_in_the_shipped_data" % section,
			float(huge["weaponMultiplier"]) == 1.0 and float(huge["armorMultiplier"]) == 1.0)
	# A side retail did not list gets nothing rather than another side's row.
	_check("a_side_retails_bonus_tables_do_not_list_gets_no_bonus_rather_than_a_neighbours",
		not bool((AutoResolve.threshold_bonus(rules, "resourceBonus", "PlayerGoblins", 50.0)
			as Dictionary)["declared"]))


func _check_retails_own_incomplete_blocks(rules: Dictionary) -> void:
	# RETAIL'S OWN DATA IS INCOMPLETE IN 18 PLACES, AND THEY ARE NAMED. Seven
	# fortress armors declare no row against a fortress and eleven Arnor armors
	# declare none against support. Retail's own fallback rule covers them, and
	# the model says WHICH block it fell back from rather than silently using the
	# default.
	var armors: Dictionary = rules.get("armors", {})
	var actually_missing: Array[String] = []
	for name in armors.keys():
		var rows: Dictionary = (armors[name] as Dictionary).get("vs", {})
		for unit_type in (rules.get("unitTypes", []) as Array):
			if not rows.has(String(unit_type)):
				actually_missing.append("%s/%s" % [name, unit_type])
	actually_missing.sort()
	var expected: Array[String] = []
	for name in ARMORS_MISSING_VS_FORTRESS:
		expected.append("%s/AutoResolveUnit_Fortress" % name)
	for name in ARMORS_MISSING_VS_SUPPORT:
		expected.append("%s/AutoResolveUnit_Support" % name)
	expected.sort()
	_check("retails_eighteen_incomplete_armor_blocks_are_exactly_the_eighteen_named_here",
		actually_missing == expected,
		"%d found, %d named" % [actually_missing.size(), expected.size()])
	var fell_back := AutoResolve.armor_multiplier(rules, "AutoResolve_MenFortressArmor",
		"AutoResolveUnit_Fortress")
	_check("a_missing_armor_row_takes_retails_documented_default_and_names_the_fallback",
		bool(fell_back["resolved"]) and float(fell_back["value"]) == 1.0
			and String(fell_back["fallback"]).contains("AutoResolve_MenFortressArmor"))
	# An armor block retail never declared is NOT silently the default: it is the
	# default WITH the substitution stated.
	var invented := AutoResolve.armor_multiplier(rules, "AutoResolve_NoSuchArmor",
		"AutoResolveUnit_Hero")
	_check("an_armor_block_retail_never_declared_names_the_substitution_it_made",
		String(invented["fallback"]).contains("AutoResolve_NoSuchArmor"))
	# Retail's own asymmetry, spot-checked on a block it authors in full: a
	# Gondor archer takes DOUBLE from cavalry and HALF from soldiers.
	var vs_cavalry := AutoResolve.armor_multiplier(rules, "AutoResolve_GondorArcherArmor",
		"AutoResolveUnit_Cavalry")
	var vs_soldier := AutoResolve.armor_multiplier(rules, "AutoResolve_GondorArcherArmor",
		"AutoResolveUnit_Soldier")
	_check("retails_gondor_archer_takes_double_from_cavalry_and_half_from_soldiers",
		float(vs_cavalry["value"]) == 2.0 and float(vs_soldier["value"]) == 0.5,
		"%.2f / %.2f" % [float(vs_cavalry["value"]), float(vs_soldier["value"])])


# --- what retail does NOT state -----------------------------------------------


## THE MODEL MUST SAY WHERE IT IS SPEAKING FOR ITSELF. Every factor this project
## chose carries a `PROJECT:` source, and the ambiguous `LevelBonus` reading
## applies NOTHING unless a caller asks for one by name.
func _check_the_model_says_what_retail_does_not(rules: Dictionary) -> void:
	var gaps: Array = rules.get("semanticGaps", [])
	var ids: Array[String] = []
	for entry in gaps:
		ids.append(String((entry as Dictionary).get("id", "?")))
	ids.sort()
	_check("the_bundle_names_every_place_retail_states_no_meaning",
		ids == ["factor.composition", "levelBonus.reading", "miss.roll", "round.order"],
		", ".join(ids))
	var attacker := _unit("Archers", "AutoResolveUnit_Archer", 5, "")
	attacker["weapon"] = "AutoResolve_GondorArcherWeapon"
	attacker["armor"] = "AutoResolve_GondorArcherArmor"
	attacker["body"] = "AutoResolve_GondorArcherHordeBody"
	var defender := _unit("Knights", "AutoResolveUnit_Cavalry", 1, "")
	defender["weapon"] = "AutoResolve_GondorKnightWeapon"
	defender["armor"] = "AutoResolve_GondorKnightArmor"
	defender["body"] = "AutoResolve_GondorKnightHordeBody"

	var strike: Dictionary = AutoResolve.strike_damage(rules, attacker, defender)
	var project_sources := 0
	var retail_sources := 0
	for factor in (strike["factors"] as Array):
		if String((factor as Dictionary)["source"]).begins_with("PROJECT:"):
			project_sources += 1
		else:
			retail_sources += 1
	_check("every_factor_in_a_strike_names_either_a_retail_file_or_this_project",
		project_sources > 0 and retail_sources > 0,
		"%d project, %d retail" % [project_sources, retail_sources])
	_check("the_composition_rule_is_labelled_as_this_projects_and_not_retails",
		String(strike["composition"]).begins_with("PROJECT:"))
	# The ambiguous LevelBonus contributes NOTHING by default, so the default
	# answer cannot contain a reading retail never stated.
	var default_factor := AutoResolve.level_bonus_multiplier(rules,
		"AutoResolve_GondorArcherWeapon", 5)
	_check("the_ambiguous_LevelBonus_applies_nothing_unless_a_reading_is_asked_for_by_name",
		float(default_factor["value"]) == 1.0
			and String(default_factor["source"]).begins_with("PROJECT:"))
	# Retail's authored ladder IS carried, whichever reading a caller picks.
	var percent := AutoResolve.level_bonus_percent(rules, "AutoResolve_GondorArcherWeapon", 5)
	_check("retails_authored_LevelBonus_percentage_is_carried_verbatim",
		float(percent["percent"]) == 25.0 and int(percent["level"]) == 5,
		"%s%% at level %d" % [percent["percent"], int(percent["level"])])
	# "next lowest level is used" - level 4 authored, level 4.5 does not exist,
	# so a level between rungs takes the rung below.
	var below := AutoResolve.level_bonus_percent(rules, "AutoResolve_GondorArcherWeapon", 9)
	_check("a_level_above_the_last_authored_rung_takes_the_last_rung_as_retail_says",
		int(below["level"]) == 5 and float(below["percent"]) == 25.0)
	var additive := AutoResolve.level_bonus_multiplier(rules,
		"AutoResolve_GondorArcherWeapon", 5, AutoResolve.LEVEL_BONUS_ADDITIVE)
	var literal := AutoResolve.level_bonus_multiplier(rules,
		"AutoResolve_GondorArcherWeapon", 5, AutoResolve.LEVEL_BONUS_LITERAL)
	_check("both_readings_of_the_ambiguous_LevelBonus_are_available_and_both_say_so",
		float(additive["value"]) == 1.25 and float(literal["value"]) == 0.25
			and String(additive["source"]).begins_with("PROJECT:")
			and String(literal["source"]).begins_with("PROJECT:"))
	# The miss chance is an EXPECTATION here and the factor says so, so nobody
	# can read the number as retail's roll.
	var hit_named := false
	for factor in (strike["factors"] as Array):
		var row: Dictionary = factor
		if String(row["name"]) == "hitChance":
			hit_named = float(row["value"]) == 0.5 and String(row["source"]).contains("ROLLS")
	_check("the_miss_chance_enters_as_a_named_expectation_and_not_as_a_silent_halving",
		hit_named)


func _check_the_model_is_deterministic_and_symmetric(rules: Dictionary) -> void:
	var left := _army(3, "AutoResolveUnit_Soldier", "AutoResolve_GondorFighterHordeBody",
		"AutoResolve_GondorSoldierWeapon", "AutoResolve_GondorSoldierArmor",
		"AutoResolve_SoldierCombatChain")
	var right := _army(3, "AutoResolveUnit_Soldier", "AutoResolve_GondorFighterHordeBody",
		"AutoResolve_GondorSoldierWeapon", "AutoResolve_GondorSoldierArmor",
		"AutoResolve_SoldierCombatChain")
	var first := AutoResolve.resolve(rules, {"armies": [left]}, {"armies": [right]})
	var second := AutoResolve.resolve(rules, {"armies": [left]}, {"armies": [right]})
	_check("the_same_battle_run_twice_gives_the_identical_outcome",
		JSON.stringify(first) == JSON.stringify(second))
	# PURITY: resolving must not have touched the caller's own arrays.
	_check("resolving_a_battle_does_not_mutate_the_armies_it_was_given",
		not (left[0] as Dictionary).has("hitpoints")
			and not (right[0] as Dictionary).has("hitpoints"))
	# SYMMETRY: retail's tables are symmetric here, so the model has to be. Two
	# identical armies must not produce a winner - if the arrangement leaked an
	# advantage to whoever strikes first, it would show up exactly here.
	_check("two_identical_armies_produce_no_winner",
		bool(first["undecided"]) and String(first["winner"]).is_empty(),
		"winner %s: %s" % [String(first["winner"]), String(first["reason"])])
	var attacker_summary: Dictionary = first["attacker"]
	var defender_summary: Dictionary = first["defender"]
	_check("two_identical_armies_end_with_mirrored_survivors",
		JSON.stringify(attacker_summary["survivors"])
			== JSON.stringify(defender_summary["survivors"]))
	# The rules dictionary itself is never written to.
	var before: Dictionary = (rules.get("totals", {}) as Dictionary).duplicate(true)
	AutoResolve.resolve(rules, {"armies": [left]}, {"armies": [right]})
	_check("resolving_a_battle_does_not_mutate_the_rules_it_read",
		JSON.stringify(before) == JSON.stringify(rules.get("totals", {})))


func _check_the_model_is_monotone_and_terminates(rules: Dictionary) -> void:
	var attacker := _unit("Soldiers", "AutoResolveUnit_Soldier", 1, "")
	attacker["weapon"] = "AutoResolve_GondorSoldierWeapon"
	attacker["body"] = "AutoResolve_GondorFighterHordeBody"
	attacker["maxHitpoints"] = 1000.0
	attacker["hitpoints"] = 1000.0
	var defender := _unit("Archers", "AutoResolveUnit_Archer", 1, "")
	defender["armor"] = "AutoResolve_GondorArcherArmor"
	defender["body"] = "AutoResolve_GondorArcherHordeBody"

	var healthy := float((AutoResolve.strike_damage(rules, attacker, defender)
		as Dictionary)["damage"])
	var hurt_attacker := attacker.duplicate(true)
	hurt_attacker["hitpoints"] = 250.0
	var hurt := float((AutoResolve.strike_damage(rules, hurt_attacker, defender)
		as Dictionary)["damage"])
	# "if the unit has 100 max hitpoints, and is currently at 75 hitpoints, all
	# the unit's attacks will be at 75% of normal damage" - retail's own worked
	# proportion, at a quarter health.
	_check("a_unit_at_a_quarter_health_does_exactly_a_quarter_damage_as_retail_states",
		absf(hurt - healthy * 0.25) < 1e-9, "%.4f vs %.4f" % [hurt, healthy * 0.25])
	# MONOTONE IN ARMOR: heavier armor can never let more damage through.
	var heavier := defender.duplicate(true)
	heavier["armor"] = "AutoResolve_GondorArcherHeavyArmor"
	var through_heavy := float((AutoResolve.strike_damage(rules, attacker, heavier)
		as Dictionary)["damage"])
	_check("retails_heavy_armor_never_lets_more_damage_through_than_its_default_armor",
		through_heavy <= healthy, "%.4f vs %.4f" % [through_heavy, healthy])
	# MONOTONE IN HANDICAP: a worse handicap can never make a unit hit harder.
	var previous := 1e30
	var monotone := true
	for level in [0, 25, 50, 75, 100]:
		var value := float((AutoResolve.strike_damage(rules, attacker, defender, {
			"attackerHandicap": AutoResolve.handicap_for(rules, level),
		}) as Dictionary)["damage"])
		if value > previous:
			monotone = false
		previous = value
	_check("a_worse_handicap_never_makes_a_unit_hit_harder", monotone)
	# TERMINATION AND DIRECTION: a side that cannot be hurt as fast as it hurts
	# must win, and the battle must END rather than run to the round bound.
	var strong := _army(6, "AutoResolveUnit_Soldier", "AutoResolve_GondorFighterHordeBody",
		"AutoResolve_GondorSoldierWeapon", "AutoResolve_GondorSoldierArmor",
		"AutoResolve_SoldierCombatChain")
	var weak := _army(1, "AutoResolveUnit_Soldier", "AutoResolve_GondorFighterHordeBody",
		"AutoResolve_GondorSoldierWeapon", "AutoResolve_GondorSoldierArmor",
		"AutoResolve_SoldierCombatChain")
	var lopsided := AutoResolve.resolve(rules, {"armies": [strong]}, {"armies": [weak]})
	_check("a_six_to_one_battle_ends_with_the_larger_side_winning",
		String(lopsided["winner"]) == "attacker",
		"%s: %s" % [String(lopsided["winner"]), String(lopsided["reason"])])
	_check("a_decided_battle_ends_well_inside_the_round_bound_rather_than_timing_out",
		int(lopsided["rounds"]) > 0 and int(lopsided["rounds"]) < AutoResolve.MAX_ROUNDS,
		"%d rounds" % int(lopsided["rounds"]))
	_check("the_winning_side_is_named_by_retails_own_end_condition",
		String(lopsided["reason"]).contains("CanBeAttacked = Yes"))
	# A battle that cannot end is reported UNDECIDED rather than given a winner.
	var stalemate := AutoResolve.resolve(rules, {"armies": [strong]}, {"armies": [weak]},
		{"maxRounds": 1})
	_check("a_battle_stopped_at_the_round_bound_is_undecided_rather_than_won",
		bool(stalemate["undecided"]) and String(stalemate["reason"]).contains("round bound"))
	# Retail's reinforcement schedule keeps a second army out of round zero, so a
	# side is not "gone" while an army is still scheduled.
	var staged := AutoResolve.resolve(rules,
		{"armies": [weak, weak]}, {"armies": [strong]}, {"maxRounds": 4})
	_check("a_side_with_a_scheduled_reinforcement_is_not_counted_as_gone",
		bool(staged["undecided"]), String(staged["reason"]))
	# Every note the outcome carries is labelled as this project's.
	var all_labelled := not (staged["notes"] as Array).is_empty()
	for note in (staged["notes"] as Array):
		if not String(note).begins_with("PROJECT:"):
			all_labelled = false
	_check("every_note_on_an_outcome_is_labelled_as_this_projects_arrangement", all_labelled)


# --- helpers ------------------------------------------------------------------


func _unit(name: String, unit_type: String, level: int, leadership: String) -> Dictionary:
	return {
		"name": name,
		"unitType": unit_type,
		"level": level,
		"leadership": leadership,
		"weapon": AutoResolve.DEFAULT_WEAPON,
		"armor": AutoResolve.DEFAULT_ARMOR,
		"body": AutoResolve.DEFAULT_BODY,
		"combatChain": AutoResolve.DEFAULT_COMBAT_CHAIN,
	}


func _army(
	count: int, unit_type: String, body: String, weapon: String, armor: String, chain: String
) -> Array:
	var army: Array = []
	for index in count:
		var unit := _unit("%s%d" % [unit_type.substr(16), index], unit_type, 1, "")
		unit["body"] = body
		unit["weapon"] = weapon
		unit["armor"] = armor
		unit["combatChain"] = chain
		army.append(unit)
	return army


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  ok   %s" % name)
	else:
		_failed += 1
		print("  FAIL %s%s" % [name, "" if detail.is_empty() else " - " + detail])
