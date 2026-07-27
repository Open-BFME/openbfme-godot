extends SceneTree

## AI-WEIGHTED DISPATCH COVERAGE - the only AI coverage number worth quoting.
##
##
## WHY THIS EXISTS
## ===============
## "N of 791 vocabulary entries implemented" answers a question nobody has. The
## retail AI does not call the vocabulary uniformly: two members
## (PLAYER_PURCHASE_SCIENCE and its condition) are 16.1% of everything it does,
## and 700 of the 791 entries are called by it ZERO times. A percentage over the
## whole table can move a long way without the retail AI becoming any more
## runnable, and can sit still while it becomes fully runnable.
##
## The number that moves with real progress is the share of its ACTUAL CALL
## SITES we can dispatch. That is what this runner measures, against a census
## committed at
##
##     game/data/retail_ai_call_census.json
##
## which is the decoded contents of the 14 AI_* WorldBuilder script libraries
## that the 301 retail maps reference through LibraryMapLists. 91 distinct names
## across 5,728 call sites (BFME2) / 6,213 (RotWK) - that is the entire retail
## AI vocabulary, not a sample of it.
##
##
## READ THIS BEFORE QUOTING ANY NUMBER THIS RUNNER PRINTS
## ======================================================
## DISPATCHABLE IS NOT EXECUTABLE, and the gap between them is currently the
## whole game. As of this writing:
##
##   * `SageScriptWorld` is a base class in which EVERY facet method refuses.
##     The only implementation of any facet is `script_world_stub.gd`, which
##     exists to serve tests.
##   * NOTHING outside `game/src/script/` references the interpreter. No world
##     is constructed for gameplay, no dispatch is registered, and no handler is
##     ever reached from the simulation. Verify with:
##         grep -rl "SageScript" game/src --include=*.gd | grep -v src/script/
##     An empty result means the interpreter is still unwired.
##
## So a member counted here as "dispatchable" means: a handler exists, it reads
## its arguments by index against the sourced signature, and it will call the
## correct world method. At runtime that call reaches a refusal, because there
## is nothing behind it yet. That is honest, useful work - argument handling is
## where this port can silently corrupt behaviour, and getting it right ahead of
## the simulation is the correct order - but it is HALF a bridge.
##
## The number below therefore tracks "how much of the retail AI vocabulary is
## correctly wired", NOT "how much of the retail AI runs". The second number is
## zero until a simulation-backed world exists and the interpreter is called
## from the match loop. Do not let this figure stand in for that one.
##
##
## WHAT COUNTS, AND WHAT DELIBERATELY DOES NOT
## ===========================================
## A name counts only if the dispatcher will really serve it. It is taken from
## `dispatch.implemented_actions()` / `implemented_conditions()`, which already
## exclude two categories on purpose:
##
##   * BLOCKED  - the name is registered, and refuses, because the simulation
##                subsystem behind it does not exist. Registered so a map using
##                it produces a structured gap instead of silence. Not coverage.
##   * DELIBERATELY REFUSED - implementable, refused on purpose (the
##                client-random family is desync-prone under lockstep).
##
## Counting either as coverage is how a port convinces itself it is finished.
## Both are reported here as their own weighted totals, so the backlog is
## visible next to the progress rather than folded into it.
##
##
## THE TWO TREES
## =============
## BFME2 and RotWK agree MEMBER-for-member: 91 names, none present in only one
## tree. They do NOT agree count-for-count - RotWK carries 6,213 call sites to
## BFME2's 5,728, and 12 members hold the entire difference, every one of them
## higher in RotWK, which is what its extra factions would predict. So coverage
## is reported per tree. They will track closely; if they ever diverge sharply
## that is a finding about the census, not a rounding artefact.
##
## This runner ASSERTS nothing about the coverage level - a threshold here would
## be a number someone has to keep lowering. It fails only when the census and
## the engine disagree about reality: a census member absent from the sourced
## vocabulary, or a census that will not load. Progress is reported; incoherence
## is a failure.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/ai_dispatch_coverage_runner.gd

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")

const CENSUS_PATH := "res://data/retail_ai_call_census.json"

## LIVENESS GUARD. A GDScript RUNTIME error aborts the enclosing function on the
## spot without propagating, so an error inside _check_census_against_vocabulary
## or _report would leave this runner printing "checks passed=1 failed=0" (or
## even a clean 2) and exiting 0 with SCRIPT ERROR lines above it.
##
## This runner does NOT have a fixed check count: a healthy run makes exactly
## two (registration clean, census resolves), but an UNHEALTHY registration
## replaces the single registration check with one failure per error message, so
## the total can legitimately exceed two. The invariant that always holds is a
## FLOOR of two - the registration verdict and the census verdict must both have
## been reached - which is what is asserted. _report makes no checks at all, so
## it is covered separately by the _report_completed sentinel below.
const MINIMUM_CHECKS := 2

var passed := 0
var failed := 0
var _report_completed := false


func _initialize() -> void:
	call_deferred("_run")


func _fail(message: String) -> void:
	failed += 1
	print("FAIL  %s" % message)


func _ok(message: String) -> void:
	passed += 1
	print("ok    %s" % message)


func _run() -> void:
	var census := _load_census()
	if census.is_empty():
		print("AI_DISPATCH_COVERAGE unavailable: census could not be loaded")
		quit(1)
		return

	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var outcome: Dictionary = Registry.register_all(dispatch)

	# A registration error means some package's handlers are NOT installed, so
	# every number below would understate coverage for a reason that has nothing
	# to do with the port's progress. Refuse to report rather than mislead.
	var registration_errors: Array = outcome.get("errors", [])
	if registration_errors.is_empty():
		_ok("handler registration clean across %d packages" % int(outcome.get("modules", 0)))
	else:
		for message: Variant in registration_errors:
			_fail("registration: %s" % String(message))

	var served := {}
	for name: Variant in dispatch.implemented_actions():
		served["action|%s" % String(name)] = true
	for name: Variant in dispatch.implemented_conditions():
		served["condition|%s" % String(name)] = true

	var blocked := {}
	for name: Variant in dispatch.blocked_names():
		blocked[String(name)] = true
	var refused := {}
	for name: Variant in dispatch.deliberately_refused():
		refused[String(name)] = true

	_check_census_against_vocabulary(census)
	_report(census, served, blocked, refused)

	print("")
	var ran := passed + failed
	if ran < MINIMUM_CHECKS:
		failed += 1
		printerr("AI_DISPATCH_COVERAGE FAIL liveness: ran %d checks, expected at least %d - a function aborted before its assertions" % [ran, MINIMUM_CHECKS])
	if not _report_completed:
		failed += 1
		printerr("AI_DISPATCH_COVERAGE FAIL liveness: _report did not run to completion - it aborted mid-way and the coverage figures above are partial")
	print("checks passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)


func _load_census() -> Dictionary:
	if not FileAccess.file_exists(CENSUS_PATH):
		push_error("AI census missing at %s" % CENSUS_PATH)
		return {}
	var text := FileAccess.get_file_as_string(CENSUS_PATH)
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		push_error("AI census at %s is not a JSON object" % CENSUS_PATH)
		return {}
	var census: Dictionary = parsed
	if not census.has("members") or typeof(census["members"]) != TYPE_ARRAY:
		push_error("AI census has no members array")
		return {}
	return census


func _check_census_against_vocabulary(census: Dictionary) -> void:
	## Every census member must exist in the sourced vocabulary. A member the
	## engine has never heard of means the decoder and the vocabulary table
	## disagree about what the retail AI calls - which would make every coverage
	## number below meaningless, and is a far more interesting finding than the
	## coverage itself.
	var unknown: Array[String] = []
	for entry: Variant in census["members"]:
		var member: Dictionary = entry
		var kind: int = (
			Vocabulary.Kind.ACTION
			if String(member.get("kind", "")) == "action"
			else Vocabulary.Kind.CONDITION
		)
		if not Vocabulary.table_for(kind).has(String(member.get("name", ""))):
			unknown.append("%s %s" % [String(member.get("kind", "")), String(member.get("name", ""))])
	if unknown.is_empty():
		_ok("all %d census members resolve in the sourced vocabulary" % census["members"].size())
	else:
		unknown.sort()
		_fail(
			"%d census members are absent from the sourced vocabulary: %s"
			% [unknown.size(), ", ".join(unknown)]
		)


func _report(census: Dictionary, served: Dictionary, blocked: Dictionary, refused: Dictionary) -> void:
	var trees: Array[String] = []
	for tree: Variant in census.get("trees", {}):
		trees.append(String(tree))
	trees.sort()

	for tree: String in trees:
		var total := 0
		var covered := 0
		var blocked_sites := 0
		var refused_sites := 0
		var unimplemented: Array = []

		for entry: Variant in census["members"]:
			var member: Dictionary = entry
			var name := String(member.get("name", ""))
			var kind := String(member.get("kind", ""))
			var sites := int((member.get("callSites", {}) as Dictionary).get(tree, 0))
			total += sites
			if served.has("%s|%s" % [kind, name]):
				covered += sites
			elif blocked.has(name):
				blocked_sites += sites
			elif refused.has(name):
				refused_sites += sites
			else:
				unimplemented.append([sites, kind, name])

		unimplemented.sort_custom(
			func(a: Array, b: Array) -> bool:
				if a[0] != b[0]:
					return a[0] > b[0]
				return String(a[2]) < String(b[2])
		)

		print("")
		print("AI_DISPATCH_COVERAGE tree=%s" % tree)
		print("  NOTE: dispatchable = correctly wired, NOT running. No")
		print("        simulation-backed world exists and the interpreter is")
		print("        not called from the match loop; see this file's header.")
		print("  call sites total        %6d" % total)
		print("  dispatchable            %6d  %6.2f%%" % [covered, _pct(covered, total)])
		print("  blocked on subsystem    %6d  %6.2f%%" % [blocked_sites, _pct(blocked_sites, total)])
		print("  deliberately refused    %6d  %6.2f%%" % [refused_sites, _pct(refused_sites, total)])
		var remaining := total - covered - blocked_sites - refused_sites
		print("  not yet implemented     %6d  %6.2f%%" % [remaining, _pct(remaining, total)])

		# The backlog, heaviest first - this list IS the work queue, ordered by
		# how much retail AI traffic each entry unblocks.
		if not unimplemented.is_empty():
			print("  top unimplemented by call sites:")
			var shown := 0
			for row: Array in unimplemented:
				if shown >= 15:
					print("    ... and %d more" % (unimplemented.size() - shown))
					break
				print("    %6d  %-9s %s" % [int(row[0]), String(row[1]), String(row[2])])
				shown += 1

	# Liveness sentinel: only set once every tree has been walked to the end. If
	# anything above aborts, this stays false and _run turns the run red instead
	# of printing partial coverage under a clean "failed=0".
	_report_completed = not trees.is_empty()


func _pct(part: int, whole: int) -> float:
	return 0.0 if whole == 0 else 100.0 * float(part) / float(whole)
