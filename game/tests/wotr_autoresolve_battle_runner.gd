extends SceneTree

## THE WIRED AUTO-RESOLVE: the dice, the determinism, and the strategic writeback.
##
## `wotr_autoresolve_runner.gd` checks what RETAIL'S TABLES SAY. This runner
## checks what THIS PROJECT DOES WITH THEM - which is a different kind of claim
## and is kept in a different file so neither can be mistaken for the other.
##
## THE CENTRAL ASSERTION is the determinism proof. Two independently constructed
## sessions, built from the same document with the same seats and the same
## rules, commit the same attack and auto-resolve it. They must produce the
## IDENTICAL outcome and the IDENTICAL strategic hash. A battle whose result
## depended on an unseeded roll would be the third instance of the desync class
## this branch has already fixed twice (867447e, and the battlefield map), and
## it is checked here the same way those were: by building two of the thing and
## comparing hashes, not by reading the code and believing it.
##
## THE DICE ARE CHECKED AGAINST THEIR OWN ARITHMETIC, not against retail. Retail
## states no seed and no stream, so there is nothing of retail's to compare a
## roll to. What CAN be checked exactly is Risk's own combinatorics - all 7,776
## outcomes of three dice against two are enumerated here and the average strike
## fraction the rules table claims is verified to the exact rational - and that
## the roller is unbiased, reproducible and keyed only by values inside the
## commitment.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/wotr_autoresolve_battle_runner.gd
## With no bundles present it still runs every check that needs no data.

const AutoResolve = preload("res://src/wotr/wotr_autoresolve.gd")
const Battle = preload("res://src/wotr/wotr_autoresolve_battle.gd")
const Rules = preload("res://src/wotr/wotr_autoresolve_rules.gd")
const Bindings = preload("res://src/wotr/wotr_autoresolve_bindings.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const BattleBridge = preload("res://src/wotr/wotr_battle.gd")

## The exact Risk 3-versus-2 distribution, over all 6^5 = 7776 equally likely
## outcomes. Enumerated by the runner rather than quoted, so a changed die size
## or tie rule changes the check instead of leaving a stale constant behind.
const RISK_OUTCOMES := 7776

## Tokens that would make this resolver non-deterministic. Every one of them is
## a real way a previous version of this project desynced or could have.
const FORBIDDEN_IN_RESOLVER := [
	"randomize(", "randi(", "randf(", "RandomNumberGenerator",
	"Time.", "OS.get_ticks", "OS.get_unix_time", "Engine.get_frames",
]

## LIVENESS ARITHMETIC.
##   27 checks need no converted data at all: 6 on the rules table, 8 on the
##      roller, 6 on the Risk contest, 2 on the source scan, 5 on the refusals.
##   24 more need retail's auto-resolve tables AND the unit bindings AND a
##      living-world document: 5 on the corrected census, 4 on the roster-to-unit
##      conversion, 13 on a whole battle and its working, 7 on the two-peer
##      determinism proof. 27 + 29 = 56.
##
## THREE OF THOSE THIRTEEN EXIST BECAUSE A MUTATION SURVIVED, and each says so
## where it stands: a factor deleted from the order, and a survivor that had
## forgotten which army it belonged to, both left every earlier check green.
const CHECKS_WITHOUT_DATA := 27
const CHECKS_WITH_DATA := 56

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING AUTO-RESOLVE: THE WIRED HALF ==")

	_check_the_rules_table_is_the_only_place_a_coefficient_lives()
	_check_the_roller_is_reproducible_and_unbiased()
	_check_the_risk_contest_matches_its_own_stated_arithmetic()
	_check_nothing_on_this_path_can_read_a_clock()
	_check_a_battle_without_a_commitment_seed_refuses()

	var model := AutoResolve.new()
	var rules_found: Dictionary = model.locate_and_load(_roots())
	var bound := Bindings.new()
	var bindings_found: Dictionary = bound.locate_and_load(_roots())
	var located: Dictionary = SessionScript.locate_document([])
	var have_everything := (
		bool(rules_found.get("ok", false))
		and bool(bindings_found.get("ok", false))
		and bool(located.get("ok", false)))

	print("auto-resolve tables : %s" % (String(rules_found.get("path", "")) if bool(rules_found.get("ok", false)) else "NONE"))
	print("unit bindings       : %s" % (String(bindings_found.get("path", "")) if bool(bindings_found.get("ok", false)) else "NONE"))
	print("living-world doc    : %s" % (String(located.get("path", "")) if bool(located.get("ok", false)) else "NONE"))

	if have_everything:
		_check_the_bindings_report_the_corrected_census(bound)
		_check_a_roster_becomes_units_with_retails_hitpoints(model.rules, bound)
		_check_a_whole_battle_resolves_and_shows_its_working(model.rules, bound)
		_check_two_independent_sessions_agree_exactly(located["document"] as Dictionary)

	var expected := CHECKS_WITH_DATA if have_everything else CHECKS_WITHOUT_DATA
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, expected, _passed, _failed])
	if total != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [total, expected])
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


# --- the moddable table --------------------------------------------------------


func _check_the_rules_table_is_the_only_place_a_coefficient_lives() -> void:
	_check("the_shipped_rules_table_is_internally_consistent",
		Rules.refusal().is_empty(), Rules.refusal())

	var owners_are_partitioned := true
	var ids: Dictionary = {}
	for row in Rules.RULES:
		var owner := String(row.get("owner", ""))
		if owner != "retail" and owner != "project":
			owners_are_partitioned = false
		if not ["data", "screenshot", "authored"].has(String(row.get("evidence", ""))):
			owners_are_partitioned = false
		if String(row.get("note", "")).strip_edges().is_empty():
			owners_are_partitioned = false
		ids[String(row.get("id", ""))] = true
	_check("every_rule_row_says_whose_number_it_is_with_evidence_and_a_note",
		owners_are_partitioned and ids.size() == Rules.RULES.size())

	var project := Rules.project_rule_ids()
	var retail := Rules.retail_rule_ids()
	_check("the_project_and_retail_rule_sets_partition_the_table_exactly",
		project.size() + retail.size() == Rules.RULES.size() and not project.is_empty()
			and not retail.is_empty(),
		"%d project + %d retail vs %d rows" % [project.size(), retail.size(), Rules.RULES.size()])

	# THE THING A MODDER ACTUALLY NEEDS: everything editable is in ONE file.
	# Asserted by naming the four decisions retail is silent about and checking
	# each one is a row here - if one ever moves into code, this reddens.
	var silent := ["level_bonus_reading", "factor_composition", "round_order", "resolution_rule"]
	var all_present := true
	for id in silent:
		if Rules.row(id).is_empty():
			all_present = false
	_check("every_place_retail_is_silent_is_an_editable_row_in_this_one_file",
		all_present, str(silent))

	# NEVER A DEFAULT. A rule this table does not carry answers nothing, because
	# answering it with a plausible number is how a silent fallback starts.
	_check("a_rule_this_table_does_not_carry_answers_nothing_rather_than_a_default",
		Rules.row("no_such_rule").is_empty() and Rules.value("no_such_rule") == null)

	var explained := Rules.explain()
	var every_line_attributed := explained.size() == Rules.RULES.size()
	for line in explained:
		if String((line as Dictionary).get("owner", "")).is_empty():
			every_line_attributed = false
		if String((line as Dictionary).get("text", "")).is_empty():
			every_line_attributed = false
	_check("the_table_explains_itself_line_by_line_for_the_battle_screen",
		every_line_attributed, "%d lines" % explained.size())


# --- the roller ----------------------------------------------------------------


func _check_the_roller_is_reproducible_and_unbiased() -> void:
	var seed_a := "a".repeat(64)
	var seed_b := "b".repeat(64)

	var first := Battle.roll_die(seed_a, "r0|a|u0|t0|a0", 6, 8)
	var again := Battle.roll_die(seed_a, "r0|a|u0|t0|a0", 6, 8)
	_check("the_same_seed_and_stream_always_produce_the_same_face",
		int(first["face"]) == int(again["face"]))

	var faces: Dictionary = {}
	var out_of_range := 0
	var fallbacks := 0
	for index in range(3000):
		var rolled := Battle.roll_die(seed_a, "sweep|%d" % index, 6, 8)
		var face := int(rolled["face"])
		faces[face] = int(faces.get(face, 0)) + 1
		if face < 1 or face > 6:
			out_of_range += 1
		if bool(rolled["fellBackToModulo"]):
			fallbacks += 1
	_check("every_face_is_inside_the_die", out_of_range == 0)
	_check("all_six_faces_appear_over_three_thousand_rolls", faces.size() == 6, str(faces))
	# Not a distribution test - a rejection-sampling test. Over 3,000 draws the
	# chance of even one rejection round is around 3000 * 4/2^32; a fallback here
	# would mean the rejection loop is broken, not that the dice were unlucky.
	_check("rejection_sampling_never_had_to_fall_back_to_a_plain_modulo", fallbacks == 0,
		"%d fallbacks" % fallbacks)

	# The counts should be near 500 each. A roller keyed only by a constant, or
	# one that ignored the stream, would put all 3,000 on one face.
	var widest := 0
	for key in faces.keys():
		widest = maxi(widest, absi(int(faces[key]) - 500))
	_check("the_faces_are_spread_rather_than_keyed_to_one_value", widest < 100,
		"widest deviation from 500 was %d" % widest)

	var differed := 0
	for index in range(200):
		if int(Battle.roll_die(seed_a, "s%d" % index, 6, 8)["face"]) \
				!= int(Battle.roll_die(seed_b, "s%d" % index, 6, 8)["face"]):
			differed += 1
	_check("a_different_seed_rolls_different_dice", differed > 120,
		"%d of 200 differed" % differed)

	var stream_differed := 0
	for index in range(200):
		if int(Battle.roll_die(seed_a, "x%d" % index, 6, 8)["face"]) \
				!= int(Battle.roll_die(seed_a, "y%d" % index, 6, 8)["face"]):
			stream_differed += 1
	_check("a_different_stream_within_one_battle_rolls_different_dice", stream_differed > 120,
		"%d of 200 differed" % stream_differed)

	# THE DICE HAVE NO CURSOR. Every die is a hash of its own name, so asking for
	# them out of order, or asking for extra ones, cannot shift any other die.
	# A stream-based generator would fail this and would then desync the moment
	# two peers planned strikes in a different order.
	var forwards: Array[int] = []
	for index in range(20):
		forwards.append(int(Battle.roll_die(seed_a, "order|%d" % index, 6, 8)["face"]))
	var backwards: Array[int] = []
	for index in range(19, -1, -1):
		backwards.append(int(Battle.roll_die(seed_a, "order|%d" % index, 6, 8)["face"]))
	backwards.reverse()
	_check("the_dice_have_no_cursor_so_asking_out_of_order_changes_nothing",
		forwards == backwards)


func _check_the_risk_contest_matches_its_own_stated_arithmetic() -> void:
	# ALL 7,776 OUTCOMES of three dice against two, enumerated. This is Risk's
	# own combinatorics and it is exact - no sampling, no tolerance.
	var attacker_dice := Rules.int_value("attacker_dice", 3)
	var defender_dice := Rules.int_value("defender_dice", 2)
	var faces := Rules.int_value("die_faces", 6)
	var pairs := Rules.int_value("pairs_compared", 2)
	var ties_to_defender := Rules.bool_value("ties_to_defender", true)
	var fractions := Rules.array_value("strike_fraction_milli_by_pairs_won")

	var won_counts: Array[int] = []
	for index in range(pairs + 1):
		won_counts.append(0)
	var total := 0
	for a0 in range(1, faces + 1):
		for a1 in range(1, faces + 1):
			for a2 in range(1, faces + 1):
				for d0 in range(1, faces + 1):
					for d1 in range(1, faces + 1):
						var attack: Array[int] = [a0, a1, a2]
						var defend: Array[int] = [d0, d1]
						attack.sort()
						attack.reverse()
						defend.sort()
						defend.reverse()
						var won := 0
						for index in range(pairs):
							var wins := attack[index] > defend[index] if ties_to_defender \
								else attack[index] >= defend[index]
							if wins:
								won += 1
						won_counts[won] += 1
						total += 1
	_check("the_enumeration_covered_every_outcome",
		total == RISK_OUTCOMES and attacker_dice == 3 and defender_dice == 2,
		"%d outcomes" % total)
	# Risk's published numbers: the attacker takes both 2890 times, splits 2611
	# and loses both 2275. If the tie rule or the die size in the table ever
	# changes, this reddens - which is the point.
	_check("the_contest_reproduces_risks_own_2890_2611_2275_split",
		won_counts[2] == 2890 and won_counts[1] == 2611 and won_counts[0] == 2275,
		str(won_counts))

	var weighted := 0
	for index in range(fractions.size()):
		weighted += won_counts[index] * int(fractions[index])
	_check("the_average_strike_fraction_is_the_5396_the_rules_table_claims",
		weighted == 8391 * 1000 / 2 and total * 1000 == 7776000,
		"%d / %d" % [weighted, total * 1000])
	# WHY THAT NUMBER MATTERS: retail's own default weapon misses half the time,
	# so retail's damage tables were authored to be halved. These dice halve them
	# too, within four points. Substituting our rule for retail's roll therefore
	# does not rescale retail's game.
	var average_milli := weighted / total
	_check("the_dice_leave_retails_damage_tables_where_retails_own_miss_chance_left_them",
		absi(average_milli - 500) <= 45, "average fraction %d milli vs retail's 500" % average_milli)

	# The contest a real battle runs must land inside that enumeration.
	var seen_fractions: Dictionary = {}
	var ties_honoured := true
	for index in range(400):
		var result := Battle.contest("c".repeat(64), "probe|%d" % index)
		seen_fractions[int(result["fractionMilli"])] = true
		for pair_row in result["pairs"] as Array:
			var pair: Dictionary = pair_row
			var expected_win := int(pair["attacker"]) > int(pair["defender"])
			if bool(pair["attackerWins"]) != expected_win:
				ties_honoured = false
	_check("a_tie_goes_to_the_defender_exactly_as_risk_says", ties_honoured)
	_check("the_contest_only_ever_returns_a_fraction_the_table_authored",
		seen_fractions.size() == fractions.size(), str(seen_fractions.keys()))


func _check_nothing_on_this_path_can_read_a_clock() -> void:
	# THE SOURCE ITSELF. A determinism argument that is only in a comment is a
	# determinism argument nobody can check, so this reads the file.
	# CODE ONLY. The resolver's own header explains that it must never read a
	# clock, and naming the thing you are forbidding is not doing it - so the
	# comment lines are stripped before the scan. A token that survives the strip
	# is a real call.
	var text := _read("res://src/wotr/wotr_autoresolve_battle.gd")
	var code: Array[String] = []
	for line in text.split("\n"):
		var stripped := String(line).strip_edges()
		if stripped.begins_with("#"):
			continue
		code.append(String(line))
	var body := "\n".join(code)
	var offenders: Array[String] = []
	for token in FORBIDDEN_IN_RESOLVER:
		if body.contains(String(token)):
			offenders.append(String(token))
	_check("the_resolver_contains_no_clock_no_engine_rng_and_no_randomize",
		offenders.is_empty(), "found: %s" % ", ".join(offenders))
	_check("the_resolver_seeds_only_from_the_commitment_digest",
		text.contains("StateScript.canonical_digest(commitment)")
			and text.contains("HashingContext.HASH_SHA256"))


func _check_a_battle_without_a_commitment_seed_refuses() -> void:
	_check("an_empty_commitment_yields_no_seed_rather_than_a_digest_of_nothing",
		Battle.seed_for({}).is_empty())
	var refused: Dictionary = Battle.resolve({}, {}, {}, "")
	_check("resolving_with_no_rules_and_no_seed_refuses_rather_than_deciding",
		not bool(refused["ok"]) and not (refused["refusals"] as PackedStringArray).is_empty())
	var no_seed: Dictionary = Battle.resolve({"armors": {}}, {}, {}, "not-a-digest")
	_check("a_seed_that_is_not_a_commitment_digest_is_refused_by_name",
		not bool(no_seed["ok"])
			and ", ".join(Array(no_seed["refusals"])).contains("reproducible"),
		", ".join(Array(no_seed["refusals"])))
	_check("a_refusal_still_carries_the_rules_table_so_a_screen_can_say_what_would_have_applied",
		(refused["rules"] as Array).size() == Rules.RULES.size())
	_check("a_template_with_no_binding_is_named_rather_than_given_default_blocks",
		Array((Battle.units_for_roster({}, [
			{"thing_template": "SomeUnitNobodyBound", "quantity": 3}
		])["unbound"] as PackedStringArray)) == ["SomeUnitNobodyBound"]
			and (Battle.units_for_roster({}, [
				{"thing_template": "SomeUnitNobodyBound", "quantity": 3}
			])["units"] as Array).is_empty())


# --- with the converted data ---------------------------------------------------


func _check_the_bindings_report_the_corrected_census(bound: Bindings) -> void:
	var census := bound.census()
	var dangling := bound.dangling()
	var live_leaderships: Dictionary = census.get("leaderships", {})
	# THE CORRECTION THAT MATTERS. Retail's object files carry 20 LIVE
	# `AutoResolveLeadership` rows across 11 distinct names, and 19 COMMENTED-OUT
	# rows across 15 more. An earlier census counted the commented ones and
	# reported 15 dangling references that do not exist. With comments correctly
	# excluded there is NOTHING dangling in any kind.
	_check("retails_live_leadership_references_are_20_across_11_names",
		int(live_leaderships.get("liveReferences", -1)) == 20
			and int(live_leaderships.get("liveDistinct", -1)) == 11,
		str(live_leaderships))
	_check("retails_commented_out_leadership_references_are_19_across_15_names",
		int(live_leaderships.get("commentedReferences", -1)) == 19
			and int(live_leaderships.get("commentedDistinct", -1)) == 15,
		str(live_leaderships))
	var nothing_dangles := true
	for key in dangling.keys():
		if not (dangling[key] as Array).is_empty():
			nothing_dangles = false
	_check("with_comments_correctly_excluded_no_reference_dangles_in_any_kind",
		nothing_dangles, str(dangling))
	_check("the_miscount_is_carried_in_the_data_so_it_cannot_be_repeated",
		(bound.manifest.get("danglingIfCommentedCounted", {}) as Dictionary).has("leaderships")
			and ((bound.manifest["danglingIfCommentedCounted"] as Dictionary)["leaderships"] as Array).size() == 15,
		str(bound.manifest.get("danglingIfCommentedCounted", {})))
	_check("angmars_live_bindings_are_a_named_finding_rather_than_a_theory",
		not bound.findings().is_empty())


func _check_a_roster_becomes_units_with_retails_hitpoints(
	rules_in: Dictionary, bound: Bindings
) -> void:
	var objects := bound.objects()
	_check("the_binding_bundle_carries_the_object_to_block_table",
		objects.size() > 100, "%d objects" % objects.size())

	# Pick a template deterministically: the lowest-sorted bound object, so the
	# check does not depend on which faction happens to be first in a dictionary.
	var names: Array[String] = []
	for key in objects.keys():
		names.append(String(key))
	names.sort()
	var sample := names[0]
	var built: Dictionary = Battle.units_for_roster(
		objects, [{"thing_template": sample, "quantity": 2}])
	_check("a_roster_entry_becomes_one_unit_per_point_of_quantity",
		(built["units"] as Array).size() == 2, sample)

	var with_health: Dictionary = Battle.with_hitpoints(rules_in, built["units"])
	var every_integer := true
	for row in with_health["units"] as Array:
		var unit: Dictionary = row
		if typeof(unit.get("hitpoints_milli", null)) != TYPE_INT:
			every_integer = false
		if int(unit["hitpoints_milli"]) != int(unit["max_hitpoints_milli"]):
			every_integer = false
	_check("hitpoints_are_integer_thousandths_because_the_strategic_hash_carries_no_floats",
		every_integer)

	# The number itself is retail's, checked against retail's own ladder rather
	# than against anything this project computed.
	var first: Dictionary = (with_health["units"] as Array)[0]
	var ladder := AutoResolve.hitpoints_at_level(
		rules_in, String(first["body"]), int(first["level"]))
	_check("the_hitpoints_are_retails_own_HitpointsAtLevel_row",
		int(first["max_hitpoints_milli"]) == roundi(float(ladder["amount"]) * 1000.0)
			and bool(ladder["declared"]),
		"%s -> %s" % [String(first["body"]), str(ladder)])


func _check_a_whole_battle_resolves_and_shows_its_working(
	rules_in: Dictionary, bound: Bindings
) -> void:
	var objects := bound.objects()
	var names: Array[String] = []
	for key in objects.keys():
		names.append(String(key))
	names.sort()
	var attacker_units := _force(rules_in, objects, names, 0, 6, 1)
	var defender_units := _force(rules_in, objects, names, 1, 6, 2)
	if attacker_units.is_empty() or defender_units.is_empty():
		_check("two_fightable_forces_could_be_built_from_the_bindings", false,
			"%d vs %d" % [attacker_units.size(), defender_units.size()])
		# Every remaining check in this function still has to run, or the
		# liveness arithmetic would silently shrink.
		for _skipped in range(12):
			_check("battle_check_skipped_because_no_force_could_be_built", false)
		return

	var seed_hex := "1234567890abcdef".repeat(4)
	var attacker := {"armies": [attacker_units], "handicap": 0, "side": "PlayerMen",
		"resource": 0.0, "sciencePoints": 0.0}
	var defender := {"armies": [defender_units], "handicap": 0, "side": "PlayerMordor",
		"resource": 0.0, "sciencePoints": 0.0}
	var outcome: Dictionary = Battle.resolve(rules_in, attacker, defender, seed_hex)
	_check("a_whole_battle_resolves", bool(outcome["ok"]),
		", ".join(Array(outcome.get("refusals", PackedStringArray()))))
	_check("the_battle_either_names_a_winner_or_says_it_is_undecided_and_never_both",
		bool(outcome["undecided"]) == String(outcome["winner"]).is_empty()
			and not String(outcome["reason"]).is_empty(),
		"%s / %s" % [String(outcome["winner"]), String(outcome["reason"])])

	# RUN IT AGAIN, FROM SCRATCH. Same seed, freshly built inputs.
	var repeat_attacker := {"armies": [_force(rules_in, objects, names, 0, 6, 1)],
		"handicap": 0, "side": "PlayerMen", "resource": 0.0, "sciencePoints": 0.0}
	var repeat_defender := {"armies": [_force(rules_in, objects, names, 1, 6, 2)],
		"handicap": 0, "side": "PlayerMordor", "resource": 0.0, "sciencePoints": 0.0}
	var repeated: Dictionary = Battle.resolve(rules_in, repeat_attacker, repeat_defender, seed_hex)
	_check("the_same_seed_over_the_same_armies_produces_a_byte_identical_result",
		StateScript.canonical_digest(_comparable(outcome))
			== StateScript.canonical_digest(_comparable(repeated)))

	# A DIFFERENT SEED IS A DIFFERENT BATTLE. If it were not, the dice would not
	# be reaching the result at all and this whole lane would be theatre.
	var other: Dictionary = Battle.resolve(rules_in, attacker, defender, "f".repeat(64))
	_check("a_different_commitment_digest_fights_a_different_battle",
		StateScript.canonical_digest(_comparable(outcome))
			!= StateScript.canonical_digest(_comparable(other)))

	# THE WORKING. Every factor says whose number it is, retail's ones name a
	# retail file, and ours say PROJECT.
	var factors_checked := 0
	var every_attributed := true
	var retail_named_a_file := 0
	var project_said_so := 0
	for entry in outcome["log"] as Array:
		var round_row: Dictionary = entry
		for strike_row in round_row.get("strikes", []) as Array:
			for factor_row in (strike_row as Dictionary)["factors"] as Array:
				var factor: Dictionary = factor_row
				factors_checked += 1
				var owner := String(factor["owner"])
				if owner != "retail" and owner != "project":
					every_attributed = false
				if owner == "retail" and String(factor["source"]).contains(".ini"):
					retail_named_a_file += 1
				if owner == "project" and String(factor["source"]).begins_with("PROJECT:"):
					project_said_so += 1
				if typeof(factor["milli"]) != TYPE_INT:
					every_attributed = false
	_check("every_factor_of_every_strike_says_whose_number_it_is",
		every_attributed and factors_checked > 0, "%d factors" % factors_checked)
	_check("retails_factors_name_the_retail_file_they_came_from",
		retail_named_a_file > 0, "%d" % retail_named_a_file)
	_check("this_projects_factors_say_so_in_words_a_player_can_read",
		project_said_so > 0, "%d" % project_said_so)

	# THE ORDER IS THE TABLE'S ORDER. `factor_order` claims to decide the
	# sequence the multipliers are applied in, and because each step truncates to
	# a whole thousandth the sequence genuinely changes the number. A table that
	# said one order while the code did another would be a modding trap.
	var order: Array = Rules.array_value("factor_order")
	var order_honoured := true
	var checked_strikes := 0
	for entry in outcome["log"] as Array:
		for strike_value in (entry as Dictionary).get("strikes", []) as Array:
			checked_strikes += 1
			var previous := -1
			for factor_value in (strike_value as Dictionary)["factors"] as Array:
				var index := order.find(String((factor_value as Dictionary)["name"]))
				if index < 0 or index <= previous:
					order_honoured = false
				previous = index
	_check("every_strike_applies_its_factors_in_the_order_the_rules_table_states",
		order_honoured and checked_strikes > 0, "%d strikes" % checked_strikes)

	# ORDER IS NOT ENOUGH. Deleting a factor from `factor_order` leaves the
	# remaining ones in a perfectly monotone order, so the check above stayed
	# green while armour silently stopped being applied at all - mutation M5
	# survived on exactly that. These four factors are not optional: retail's
	# damage row and armour row are the whole model, and the other two are the
	# rules this project owns. A strike missing any of them is not a strike this
	# build knows how to explain.
	var required := ["damagePerRound", "armor", "reduceAttackWhenHurt", "dice"]
	var missing: Array[String] = []
	for entry in outcome["log"] as Array:
		for strike_value in (entry as Dictionary).get("strikes", []) as Array:
			var present: Dictionary = {}
			for factor_value in (strike_value as Dictionary)["factors"] as Array:
				present[String((factor_value as Dictionary)["name"])] = true
			for name in required:
				if not present.has(name) and not missing.has(String(name)):
					missing.append(String(name))
	_check("no_strike_can_quietly_lose_retails_damage_row_its_armour_row_or_the_dice",
		missing.is_empty() and checked_strikes > 0, "missing: %s" % ", ".join(missing))

	# THE DICE ARE VISIBLE. A player must be able to see what was rolled.
	var saw_dice := false
	for entry in outcome["log"] as Array:
		for strike_row in (entry as Dictionary).get("strikes", []) as Array:
			var contest: Dictionary = (strike_row as Dictionary)["contest"]
			if not contest.is_empty() and (contest["attackerDice"] as Array).size() == 3 \
					and (contest["defenderDice"] as Array).size() == 2:
				saw_dice = true
	_check("the_result_carries_the_actual_dice_so_the_working_can_be_checked_by_eye", saw_dice)

	# ATTRITION IS A LIST OF SURVIVORS WITH REAL HITPOINTS, not a boolean.
	var survivors: Array = (outcome["attacker"] as Dictionary)["survivors"]
	var sane := true
	for row in survivors:
		var unit: Dictionary = row
		if int(unit["hitpoints_milli"]) > int(unit["max_hitpoints_milli"]):
			sane = false
		if typeof(unit["hitpoints_milli"]) != TYPE_INT:
			sane = false
	_check("the_outcome_is_attrition_rather_than_a_winner_flag",
		sane and (survivors.size() + (outcome["attacker"] as Dictionary)["lost"].size()) > 0,
		"%d survivors" % survivors.size())

	# EVERY SURVIVOR KNOWS WHOSE IT IS, and the strategic layer refuses one that
	# does not. Mutation M3 zeroed `army_id` on every survivor and NOTHING went
	# red: both peers did the same wrong thing, so the hashes still matched and
	# the attrition was written to army 0 - which does not exist - meaning a
	# whole battle's casualties would have been silently discarded. Agreement is
	# not correctness, and this is the check that says so.
	var wrong_army := 0
	for row in survivors:
		if int((row as Dictionary).get("army_id", 0)) != 1:
			wrong_army += 1
	_check("every_survivor_carries_the_army_it_actually_belongs_to",
		wrong_army == 0 and not survivors.is_empty(),
		"%d of %d survivors named the wrong army" % [wrong_army, survivors.size()])
	# And the writeback refuses a mis-keyed unit rather than moving it.
	var state := StateScript.new()
	_check("apply_attrition_refuses_a_survivor_that_belongs_to_another_army",
		not state.apply_attrition(1, [{"army_id": 2, "hitpoints_milli": 10}]))


## A force of `count` units drawn deterministically from the bound objects.
## `offset` picks a different slice for each side; `level` is retail's own.
func _force(
	rules_in: Dictionary, objects: Dictionary, names: Array[String],
	offset: int, count: int, level: int
) -> Array:
	var entries: Array = []
	var taken := 0
	var index := offset
	while taken < count and index < names.size():
		var name := names[index]
		var binding: Dictionary = objects[name]
		# Only units with a body retail authored hitpoints for can fight; a body
		# whose ladder does not answer would arrive at zero and die instantly,
		# which would be this runner inventing a rout.
		var ladder := AutoResolve.hitpoints_at_level(
			rules_in, String(binding.get("body", "")), level)
		if bool(ladder.get("declared", false)) and float(ladder["amount"]) > 0.0:
			entries.append({"thing_template": name, "quantity": 1})
			taken += 1
		index += 2
	if entries.is_empty():
		return []
	var built: Dictionary = Battle.units_for_roster(objects, entries, level)
	var with_health: Dictionary = Battle.with_hitpoints(rules_in, built["units"])
	var units: Array = with_health["units"]
	for row in units:
		(row as Dictionary)["army_id"] = 1 + offset
	return units


## Everything about an outcome that MUST match between two peers. The rules
## explanation is dropped only because it is a constant, not because it varies.
func _comparable(outcome: Dictionary) -> Dictionary:
	return {
		"winner": outcome["winner"],
		"undecided": outcome["undecided"],
		"reason": outcome["reason"],
		"rounds": outcome["rounds"],
		"log": outcome["log"],
		"attacker": outcome["attacker"],
		"defender": outcome["defender"],
	}


# --- THE DETERMINISM PROOF -----------------------------------------------------


## TWO SESSIONS, BUILT SEPARATELY, PLAYING THE SAME CAMPAIGN. This is the check
## the whole lane rests on: if a battle's outcome could differ between two peers
## that agree on strategic state, the campaign desyncs, and no amount of careful
## reading of the resolver would prove it does not.
func _check_two_independent_sessions_agree_exactly(document: Dictionary) -> void:
	var first := _seated_session(document)
	var second := _seated_session(document)
	if first == null or second == null:
		for _skipped in range(7):
			_check("determinism_proof_skipped_because_a_session_could_not_be_seated", false)
		return

	_check("two_independently_built_sessions_start_on_the_same_strategic_hash",
		first.state.state_hash() == second.state.state_hash(),
		"%s vs %s" % [first.state.state_hash().substr(0, 16), second.state.state_hash().substr(0, 16)])

	var target := _first_attackable(first, second)
	if target.is_empty():
		for _skipped in range(6):
			_check("determinism_proof_skipped_because_no_legal_attack_existed", false)
		return

	# MAKE IT A REAL BATTLE. Retail seats the two sides at opposite ends of
	# Middle-earth, and a seat may only march inside its own territory, so the
	# first legal attack in a shipped scenario is almost always onto an empty
	# border region - which resolves in zero rounds and rolls no dice, and would
	# make the determinism proof below prove nothing about the dice. Both
	# sessions therefore garrison the target IDENTICALLY, through the strategic
	# layer's own `place_army`, before committing. That is a constructed
	# situation and it is said so here; what it is NOT is a constructed RESULT.
	_garrison(first, target)
	_garrison(second, target)

	var maps: Array = ["bfme2.map.dagorlad", "bfme2.map.fords-of-isen"]
	var committed_first: Dictionary = first.commit_attack(target, maps)
	var committed_second: Dictionary = second.commit_attack(target, maps)
	_check("both_sessions_mint_the_same_commitment_for_the_same_attack",
		bool(committed_first.get("ok", false)) and bool(committed_second.get("ok", false))
			and StateScript.canonical_digest(committed_first["commitment"])
				== StateScript.canonical_digest(committed_second["commitment"]),
		", ".join(Array(committed_first.get("refusals", PackedStringArray()))))

	_check("the_commitment_is_version_3_and_carries_both_handicaps_and_the_battle_type",
		int((committed_first["commitment"] as Dictionary).get("schema_version", -1)) == 3
			and (committed_first["commitment"] as Dictionary).has("attacker_handicap")
			and (committed_first["commitment"] as Dictionary).has("defender_handicap")
			and (committed_first["commitment"] as Dictionary).has("battle_type")
			and (committed_first["commitment"] as Dictionary).has("battle_type_priority"),
		str((committed_first["commitment"] as Dictionary).keys()))

	var resolved_first: Dictionary = first.auto_resolve_pending_battle()
	var resolved_second: Dictionary = second.auto_resolve_pending_battle()
	_check("both_sessions_seed_the_dice_from_the_same_commitment",
		String(resolved_first.get("seed", "")) == String(resolved_second.get("seed", ""))
			and String(resolved_first.get("seed", "")).length() == 64,
		String(resolved_first.get("seed", "")))
	_check("both_sessions_fight_the_identical_battle",
		bool(resolved_first.get("ok", false)) and bool(resolved_second.get("ok", false))
			and StateScript.canonical_digest(_comparable(resolved_first["outcome"] as Dictionary))
				== StateScript.canonical_digest(_comparable(resolved_second["outcome"] as Dictionary)),
		", ".join(Array(resolved_first.get("refusals", PackedStringArray()))))
	# THE ONE THAT MATTERS MOST: not merely the same battle, the same CAMPAIGN
	# afterwards. Attrition, ownership, army positions, the lot.
	_check("both_sessions_hold_the_identical_strategic_hash_after_the_battle",
		first.state.state_hash() == second.state.state_hash(),
		"%s vs %s" % [first.state.state_hash().substr(0, 16), second.state.state_hash().substr(0, 16)])
	# AND IT WAS A REAL BATTLE. An undefended region resolves in zero rounds
	# with no dice at all, and two peers would agree about that trivially - so
	# the proof above would be worth nothing if this is what it had picked.
	var fought: int = int((resolved_first.get("outcome", {}) as Dictionary).get("rounds", 0))
	_check("the_determinism_proof_fought_a_contested_battle_with_real_dice",
		int(fought) > 0
			and not ((resolved_first["outcome"] as Dictionary)["log"] as Array).is_empty(),
		"%d rounds" % int(fought))


## Put a garrison in `region_id` for whoever owns it, if it has none. The roster
## is the lowest-sorted one that actually fields auto-resolve units, so both
## sessions pick the same one without being told which.
func _garrison(session: SessionScript, region_id: String) -> void:
	if not session.state.armies_in_region(region_id).is_empty():
		return
	var owner := session.state.owner_of(region_id)
	if owner == StateScript.NEUTRAL:
		return
	var names: Array[String] = []
	for key in session.state.roster_units.keys():
		names.append(String(key))
	names.sort()
	for name in names:
		if (session.state.roster_units[name] as Array).is_empty():
			continue
		session.state.place_army(owner, region_id, name)
		return


## A seated, auto-resolve-capable session. The scenario is found by TRYING each
## one the document carries, in file order, and keeping the first that seats two
## players with ownership - rather than by hard-coding a name, so this survives
## a different document.
func _seated_session(document: Dictionary) -> SessionScript:
	# Two seats, both AI, with DIFFERENT handicaps so the handicap is proved to
	# ride the commitment rather than being quietly dropped.
	var seats := [
		{"template": "PlayerMen", "team": 1, "controller": "ai", "handicap": 0},
		{"template": "PlayerMordor", "team": 2, "controller": "ai", "handicap": 25},
	]
	var rules := {"battle_type": "auto_resolve", "battle_type_priority": "auto_resolve"}
	for scenario_row in document.get("scenarios", []) as Array:
		var scenario := scenario_row as Dictionary
		if int(scenario.get("maxPlayers", 0)) < 2:
			continue
		if (scenario.get("ownershipSets", []) as Array).size() < 2:
			continue
		var session := SessionScript.new()
		session.load_auto_resolve(_roots())
		if session.begin(document, String(scenario.get("regionCampaign", "")),
				String(scenario.get("name", "")), seats, rules):
			return session
	return null


## The first legal attack the active seat can make, after marching if it must.
## Retail seats each side deep inside its own territory, so turn one often has
## no legal attack at all until an army moves.
## `mirror` receives EVERY march this function performs, in the same order.
##
## Without it this helper marched only `session`, then the caller committed on
## BOTH - so the second peer was asked to attack from a region its armies had
## never reached. That passed under the RotWK document purely because its seat 0
## starts already on a front and no march is needed; under BFME2, where the seat
## starts inland and must march, the second commit refused and six checks failed
## downstream of it.
##
## Two peers that are supposed to be independently constructed and IDENTICAL
## must receive identical inputs. Mirroring the marches is what makes that true;
## it strengthens the proof rather than relaxing it, because the peers now reach
## the commitment by the same route instead of one being handed the answer.
func _first_attackable(session: SessionScript, mirror: SessionScript = null) -> String:
	# A DEFENDED TARGET IS PREFERRED. An undefended region is a legal attack and
	# resolves correctly, but it fights no rounds and rolls no dice - so a
	# determinism proof that happened to pick one would prove nothing about the
	# dice, which are the whole point.
	var fallback := ""
	var fallback_from := ""
	for _step in range(40):
		for from_region in session.staging_regions():
			for target in session.attack_targets(String(from_region)):
				if not session.state.armies_in_region(String(target)).is_empty():
					session.selected_region = String(from_region)
					session.selected_target = String(target)
					if mirror != null:
						mirror.selected_region = String(from_region)
						mirror.selected_target = String(target)
					return String(target)
				if fallback.is_empty():
					fallback = String(target)
					fallback_from = String(from_region)
		var marched := false
		for from_region in session.staging_regions():
			for to_region in session.movement_targets(String(from_region)):
				if bool(session.move_armies(String(from_region), String(to_region)).get("ok", false)):
					if mirror != null:
						mirror.move_armies(String(from_region), String(to_region))
					marched = true
					break
			if marched:
				break
		if not marched:
			break
	# Nothing defended was ever in reach. An undefended target is still a legal
	# attack and still proves the two peers agree - it just proves it without
	# dice, which the caller checks separately and reports.
	#
	# RE-SCANNED FROM CURRENT STATE, NOT REPLAYED FROM THE RECORDED ONE. The
	# fallback above is noted on the FIRST pass, and the loop then marches up to
	# forty times - so by the time we get here the seat may no longer hold an
	# army in `fallback_from` at all. Committing that stale pair produced
	# "seat 0 cannot legally attack <region> from any region it holds", which
	# reads like a rules bug and is actually a bookkeeping one. Ask the session
	# what is legal NOW.
	for from_region in session.staging_regions():
		for candidate in session.attack_targets(String(from_region)):
			session.selected_region = String(from_region)
			session.selected_target = String(candidate)
			if mirror != null:
				mirror.selected_region = String(from_region)
				mirror.selected_target = String(candidate)
			return String(candidate)
	return ""


# --- plumbing ------------------------------------------------------------------


func _read(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ""
	var text := file.get_as_text()
	file.close()
	return text


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS %s" % name)
	else:
		_failed += 1
		print("  FAIL %s%s" % [name, "" if detail.is_empty() else " - " + detail])
