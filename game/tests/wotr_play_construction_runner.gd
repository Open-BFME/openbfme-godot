extends SceneTree

## PLAY SEVERAL TURNS OF THE REAL CAMPAIGN AND BUILD THINGS.
##
## Not an assertion harness - `wotr_construction_runner.gd` is that. This is the
## thing a person does after writing a feature: sit down at the real map, with
## the real converted bundles, and USE it. It seats retail's own
## `WOTRScenario045` ("The Rise of the Witch-king"), plays ten turns, has the
## human seat build on every free plot it can afford, lets the two AI seats take
## their own turns through the same doors, and prints the board after each one.
##
## It exists in the repository rather than as a throwaway because a defect this
## lane closed - "I cannot click on the buildings or build them" - is a defect
## about the CAMPAIGN working, not about a unit test passing, and the cheapest
## way to keep proving that is a script anyone can run.
##
## It asserts only the two things that would make the transcript a lie: that
## treasure actually moved, and that structures actually stand. Everything else
## it PRINTS, for a person to read.

const SessionScript = preload("res://src/wotr/wotr_session.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

const TURNS := 10

var failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var located: Dictionary = SessionScript.locate_document([])
	if not bool(located.get("ok", false)):
		printerr("PLAY: no living-world document. %s" % String(located.get("reason", "")))
		quit(1)
		return

	var session := SessionScript.new()
	session.document_path = String(located.get("path", ""))
	session.document_source = String(located.get("source", ""))
	var seats := [
		{"template": "PlayerMen", "team": 1, "controller": "human"},
		{"template": "PlayerElves", "team": 1, "controller": "ai"},
		{"template": "PlayerAngmar", "team": 2, "controller": "ai"},
	]
	if not session.begin(located["document"] as Dictionary, "DefaultCampaign",
			"WOTRScenario045", seats):
		printerr("PLAY: the scenario would not start: %s" % str(session.refusals))
		quit(1)
		return
	session.load_auto_resolve([])
	session.load_ai_template([])

	print("")
	print("================================================================")
	print(" WAR OF THE RING - WOTRScenario045, Men (you) + Elves vs Angmar")
	print("================================================================")
	if session.buildings == null:
		printerr("PLAY: no building catalogue: %s" % session.building_catalogue_reason)
		quit(1)
		return
	for line in session.buildings.describe_load():
		print("  %s" % line)
	print("  AI template: %s" % ("retail's own weights" if session.ai.loaded
		else "NOT STAGED - " + session.ai_template_reason.substr(0, 90)))
	print("")

	var opening_treasure := session.state.treasure(0)
	var opening_structures := _count_structures(session.state)
	print("  Opening treasury: seat 0 = %d, seat 1 = %d, seat 2 = %d" % [
		session.state.treasure(0), session.state.treasure(1), session.state.treasure(2)])
	print("  Structures already standing (the scenario's own SpawnBuildings rows):")
	for line in _structure_lines(session.state):
		print("    %s" % line)
	print("")

	var raised := 0
	for turn in range(TURNS):
		var seat := session.state.active_player()
		var who := String((session.state.players[seat] as Dictionary).get("template", ""))
		print("-- TURN %d -- seat %d (%s), treasury %d, income +%d --" % [
			turn + 1, seat, who, session.state.treasure(seat),
			int(session.income_report(seat).get("total", 0))])
		if session.active_seat_is_ai():
			var report: Dictionary = session.run_ai_turn([])
			for line in report.get("narrative", PackedStringArray()) as PackedStringArray:
				print("   AI: %s" % String(line))
			raised += (report.get("builds", []) as Array).size()
			for row in report.get("builds", []) as Array:
				var build := row as Dictionary
				print("       (retail BuildingScore %d, +%d per turn thereafter)" % [
					int(build.get("retail_score", 0)), int(build.get("income_per_turn", 0))])
			if not bool(report.get("ok", false)):
				for refusal in report.get("refusals", PackedStringArray()) as PackedStringArray:
					print("   AI refused: %s" % String(refusal))
				session.state.advance_turn()
			continue

		# THE HUMAN SEAT'S TURN, played the way the build ring will play it:
		# pick a region with a free plot, read the offer, take the best thing it
		# can afford, and say what every refusal was.
		raised += _play_human_build_phase(session, seat)
		print("   income after building: +%d per turn" % int(session.income_report(seat).get("total", 0)))
		session.state.advance_turn()

	print("")
	print("================================================================")
	print(" AFTER %d TURNS" % TURNS)
	print("================================================================")
	print("  Treasury: seat 0 = %d (opened on %d), seat 1 = %d, seat 2 = %d" % [
		session.state.treasure(0), opening_treasure,
		session.state.treasure(1), session.state.treasure(2)])
	print("  Income:   seat 0 = +%d per turn" % int(session.income_report(0).get("total", 0)))
	for line in _structure_lines(session.state):
		print("    %s" % line)
	var closing_structures := _count_structures(session.state)
	print("  Structures: %d -> %d (%d raised this session)" % [
		opening_structures, closing_structures, raised])
	print("  Strategic hash: %s" % session.state.state_hash())

	# THE TWO THINGS THAT WOULD MAKE THE TRANSCRIPT ABOVE A LIE.
	_expect("treasure moved", session.state.treasure(0) != opening_treasure
		or session.state.treasure(2) != 3000)
	_expect("structures stand that did not before", closing_structures > opening_structures)
	# AND THE ONE THAT WOULD MAKE IT UNPLAYABLE ON A SECOND MACHINE.
	var adopted := SessionScript.new()
	_expect("the board survives a handoff with its structures and its purse",
		adopted.adopt_handoff(session.handoff_payload())
			and adopted.state.state_hash() == session.state.state_hash())
	print("")
	print("PLAY_RESULT failures=%d" % failures)
	quit(0 if failures == 0 else 1)


## Build on every free plot the seat can afford, best value first, and REPORT
## every refusal - a turn that could not build is as interesting as one that did.
func _play_human_build_phase(session, seat: int) -> int:
	var raised := 0
	var state: StateScript = session.state
	for region_id in state.regions_owned_by(seat):
		if state.free_plot(region_id) < 0:
			continue
		var options: Array[Dictionary] = session.build_options(region_id)
		if options.is_empty():
			continue
		# Prefer what pays for itself: income first, then the cheapest. That is
		# THIS SCRIPT'S taste, not the layer's - `wotr_ai.gd` uses retail's own
		# BuildingScore weights and this file is a person clicking.
		options.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			if int(a["income_per_turn"]) != int(b["income_per_turn"]):
				return int(a["income_per_turn"]) > int(b["income_per_turn"])
			return int(a["cost"]) < int(b["cost"]))
		var took := false
		for row in options:
			if not bool(row["can_build"]):
				continue
			var built: Dictionary = session.commit_build(region_id, String(row["building"]))
			if not bool(built["ok"]):
				print("   REFUSED %s in %s: %s" % [String(row["building"]), region_id,
					String((built["refusals"] as PackedStringArray)[0])])
				continue
			print("   BUILT %s on plot %d of %s for %d (treasury %d -> %d)" % [
				String(built["building"]), int(built["plot"]), region_id,
				int(built["cost"]), int(built["treasury_before"]), int(built["treasury_after"])])
			raised += 1
			took = true
			break
		if not took and not options.is_empty():
			print("   nothing buildable in %s: %s" % [region_id, String(options[0]["refusal"])])
	return raised


func _structure_lines(state: StateScript) -> PackedStringArray:
	var lines: Array[String] = []
	for region_id in state.world.region_ids:
		var standing := state.structures_in_region(region_id)
		if standing.is_empty():
			continue
		var parts: Array[String] = []
		for row in standing:
			var record := row as Dictionary
			parts.append("plot %d: %s (%s, seat %d%s)" % [
				int(record["plot"]), String(record["building"]), String(record["type"]),
				int(record["owner"]),
				", from scenario token %s" % String(record["token"])
					if not String(record["token"]).is_empty() else ""])
		lines.append("%s [%d/%d plots] %s" % [
			region_id, standing.size(), state.plot_count(region_id), "; ".join(parts)])
	return PackedStringArray(lines)


func _count_structures(state: StateScript) -> int:
	var total := 0
	for region_id in state.world.region_ids:
		total += state.structures_in_region(region_id).size()
	return total


func _expect(what: String, condition: bool) -> void:
	if condition:
		print("  OK   %s" % what)
	else:
		failures += 1
		printerr("  FAIL %s" % what)
