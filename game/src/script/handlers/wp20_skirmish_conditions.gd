extends RefCounted

## WP20-skirmish-conditions - the game-mode and proximity CONDITIONS that gate
## skirmish map init scripts.
##
## SCOPE, AND WHY THESE FIVE
## =========================
## IS_GAME_MODE_ACTIVE guards every SkirmishGollum init script in the retail
## skirmish maps: without it, the spawn scripts never fire even once the spawn
## ACTION works (WP19-map-spawn owns that action; this file owns none of its
## names). The other four are the recency/proximity gates those same map
## scripts poll around it. All five are conditions; this package registers no
## actions.
##
## Table rows served, verbatim from script_condition_table.gd (name | params |
## category | game | confidence):
##
##   "IS_GAME_MODE_ACTIVE|TEXT_STRING|GameType|BFME2ROTWK_PLUS|S"          (row 92)
##   "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS|EVA_EVENT,REAL|Player|BFME2ROTWK_PLUS|S" (row 82)
##   "IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT|PLAYER,REAL,EVA_EVENT,COMPARISON,INT|Player|BFME2ROTWK_PLUS|S" (row 94)
##   "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT|PLAYER,COMPARISON,INT,REAL,UNIT|Player|SHARED|S" (row 181)
##   "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION|PLAYER,MODEL_CONDITION,COMPARISON,INT|Player|SHARED|S" (row 177)
##
## World surface used (all already implemented by the retail slice adapter):
##   meta().game_mode(mode)
##   players().eva_event_played_within(player, event, seconds)
##   players().units_near_last_eva_event(player, radius)
##   players().object_count_within_distance(player, object_type, origin, distance)
##   players().object_count_with_model_condition(player, model_condition)
##
## THE ARGUMENT TRAP
## =================
## Every read below is BY INDEX, with the signature written directly above it.
## Two of these signatures put the integer-valued COMPARISON and INT slots side
## by side, where no type-search could tell them apart - read backwards,
## "player has > 2 units near the tower" becomes "player has == <distance>
## units", answered confidently on every poll. The runner pins each signature
## with fixtures chosen so a swapped read gives a DIFFERENT answer.
##
## CONDITIONS MAY NOT GUESS, AND MAY NOT WRITE
## ===========================================
## All five gate polled init scripts, evaluated an unpredictable number of
## times per match. They write ctx.result ONLY - never the env, never the
## world. And a read the world cannot answer must never come back as a plain
## false: "the game mode is not Skirmish" and "the world does not know its
## game mode" are different answers, and only the first may keep (or release)
## a spawn gate on the record's own authority. Every condition checks
## SageWorldQuery.ok first and returns WORLD_REFUSED otherwise, which the
## dispatcher turns into CONDITION_FALLBACK (false) *and* a structured gap.

const PACKAGE := "WP20-skirmish-conditions"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS authors NO player argument: like the
## retail engine, the question is asked as the script-owning player. The world
## owns that resolution - this is the same "<This Player>" token retail map
## scripts author in PLAYER slots, which the concrete world resolves to the
## bound script player and a world with no bound player REFUSES (loudly, as a
## structured gap) rather than guessing a seat.
const THIS_PLAYER := "<This Player>"


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.condition("IS_GAME_MODE_ACTIVE", _is_game_mode_active)
	reg.condition(
		"HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS", _has_eva_event_played_in_last_n_seconds
	)
	reg.condition(
		"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
		_units_near_last_eva_event_comparison
	)
	reg.condition(
		"PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", _units_distance_from_object
	)
	reg.condition(
		"PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", _objects_with_model_condition
	)


# --- Shared tails -----------------------------------------------------------


static func _unanswered(ctx: Dictionary, query: SageWorldQuery) -> int:
	## Every condition in this file takes this path when the world could not
	## answer. It must NEVER be replaced by "return false": a refusal is
	## recoverable, a silently wrong answer is not (see the class comment).
	ctx["detail"] = query.detail
	return Dispatch.Status.WORLD_REFUSED


static func _operator_out_of_table(ctx: Dictionary, name: String, comparison: int) -> bool:
	## The comparison operator is consumed by THIS file (the world methods
	## carry no comparison parameter), and ParamTypes.compare_int answers
	## `false` for any operator it does not know - which would turn "this
	## handler does not understand the operator" into a confident "no" at a
	## spawn gate. An out-of-table operator is therefore BAD_ARGUMENTS,
	## refused loudly and never guessed (same verdict as WP17's
	## PLAYER_HAS_OBJECT_COMPARISON).
	if comparison >= ParamTypes.COMPARE_LESS and comparison <= ParamTypes.COMPARE_NOT_EQUAL:
		return false
	ctx["detail"] = (
		"%s was authored with comparison operator %d, outside the sourced "
		+ "COMPARISON table (0..5); compare_int would silently answer false "
		+ "for it, so it is refused instead"
	) % [name, comparison]
	return true


# --- Conditions ---------------------------------------------------------------
#
# Every one of them may answer true, may answer false, or may refuse - and the
# third is not the second.


static func _is_game_mode_active(ctx: Dictionary) -> int:
	# IS_GAME_MODE_ACTIVE(TEXT_STRING)
	#   table row 92: "IS_GAME_MODE_ACTIVE|TEXT_STRING|GameType|BFME2ROTWK_PLUS|S"
	#   arg 0: TEXT_STRING - the mode token, text payload.
	#
	# This is the guard on every SkirmishGollum init script. The mode string
	# passes through VERBATIM - no case folding, no trimming - per the world's
	# argument conventions; the world owns what its current mode token is and
	# whether the comparison holds. A world that does not know its mode
	# refuses, which keeps the init gate shut WITH a structured gap instead of
	# silently never spawning.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).meta().game_mode(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


static func _has_eva_event_played_in_last_n_seconds(ctx: Dictionary) -> int:
	# HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS(EVA_EVENT, REAL)
	#   table row 82: "HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS|EVA_EVENT,REAL|Player|BFME2ROTWK_PLUS|S"
	#   arg 0: EVA_EVENT - the event name, text payload
	#   arg 1: REAL      - the window in SECONDS, real payload
	#
	# NO PLAYER IS AUTHORED. The signature carries only the event and the
	# window, so the acting player is the script-owning player, passed as the
	# world's "<This Player>" token (see the constant above). The SECONDS pass
	# through as authored: the world method's own parameter is `seconds`
	# (players.eva_event_played_within), so the tick conversion the world
	# needs is the world's, not this handler's - converting here would double
	# it.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).players().eva_event_played_within(
		THIS_PLAYER, args.text(0), args.real(1)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


static func _units_near_last_eva_event_comparison(ctx: Dictionary) -> int:
	# IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT
	#   (PLAYER, REAL, EVA_EVENT, COMPARISON, INT)
	#   table row 94: "...|PLAYER,REAL,EVA_EVENT,COMPARISON,INT|Player|BFME2ROTWK_PLUS|S"
	#   arg 0: PLAYER     - whose units are counted, text payload
	#   arg 1: REAL       - the radius, real payload
	#   arg 2: EVA_EVENT  - the event name, text payload (see below)
	#   arg 3: COMPARISON - the operator, integer payload
	#   arg 4: INT        - the threshold, integer payload
	#
	# THE ADJACENT-INTEGER TRAP: arguments 3 and 4 both live in the `integer`
	# payload field, so only position separates the operator from the
	# threshold. The comparison is evaluated HERE against the count the world
	# reports - the world method carries no comparison parameter.
	#
	# THE EVA_EVENT ARGUMENT IS NOT FORWARDED, by the world surface's own
	# design: players.units_near_last_eva_event(player, radius) keys on the
	# player's LAST-PLAYED EVA event location and carries no per-event slot
	# (script_world.gd:766, annotated for exactly this condition). The
	# argument is still arity- and position-checked, so it cannot silently
	# absorb a neighbouring slot; if a map is ever found whose behaviour
	# depends on distinguishing WHICH event's location, that is a
	# facet-signature finding to report, not a slot to guess around.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(3)
	if _operator_out_of_table(
		ctx,
		"IS_NUM_OF_UNITS_BELONGING_TO_PLAYER_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION_COMPARISON_INT",
		comparison
	):
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).players().units_near_last_eva_event(
		args.text(0), args.real(1)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare_int(query.as_int(), comparison, args.integer(4))
	return Dispatch.Status.OK


static func _units_distance_from_object(ctx: Dictionary) -> int:
	# PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT(PLAYER, COMPARISON, INT, REAL, UNIT)
	#   table row 181: "...|PLAYER,COMPARISON,INT,REAL,UNIT|Player|SHARED|S"
	#   arg 0: PLAYER     - whose units are counted, text payload
	#   arg 1: COMPARISON - the operator, integer payload
	#   arg 2: INT        - the threshold, integer payload
	#   arg 3: REAL       - the distance, real payload
	#   arg 4: UNIT       - the named ORIGIN object, text payload
	#
	# Arguments 1 and 2 are the adjacent-integer pair again; arguments 3 and 4
	# add a second trap - the distance (real) sits right next to the origin
	# name (text), and sage_scb.py populates integer, real AND text for every
	# argument, so a wrong-slot read succeeds silently. Everything is read
	# positionally per the row above.
	#
	# NO OBJECT TYPE IS AUTHORED, so the world's documented empty-type
	# "anything at all" form is passed: players.object_count_within_distance
	# (player, object_type, origin, distance) counts every unit when
	# object_type is "" - the same ""-means-every-type convention the surface
	# already defines on players.building_count. The condition's own signature
	# rules this: it counts UNITS of the player, filtered only by distance
	# from the named object.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if _operator_out_of_table(
		ctx, "PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT", comparison
	):
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).players().object_count_within_distance(
		args.text(0), "", args.text(4), args.real(3)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare_int(query.as_int(), comparison, args.integer(2))
	return Dispatch.Status.OK


static func _objects_with_model_condition(ctx: Dictionary) -> int:
	# PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION(PLAYER, MODEL_CONDITION, COMPARISON, INT)
	#   table row 177: "...|PLAYER,MODEL_CONDITION,COMPARISON,INT|Player|SHARED|S"
	#   arg 0: PLAYER          - whose objects are counted, text payload
	#   arg 1: MODEL_CONDITION - the model-condition flag name, text payload
	#   arg 2: COMPARISON      - the operator, integer payload
	#   arg 3: INT             - the threshold, integer payload
	#
	# The world method players.object_count_with_model_condition(player,
	# model_condition) is the same fold WP01's
	# SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION reads
	# through - one counting method, with the comparison evaluated here from
	# the trailing operator/threshold pair (adjacent integers, positional).
	# The model-condition name passes through verbatim; the world owns its
	# flag namespace.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(2)
	if _operator_out_of_table(
		ctx, "PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION", comparison
	):
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (
		(ctx["world"] as SageScriptWorld).players().object_count_with_model_condition(
			args.text(0), args.text(1)
		)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare_int(query.as_int(), comparison, args.integer(3))
	return Dispatch.Status.OK
