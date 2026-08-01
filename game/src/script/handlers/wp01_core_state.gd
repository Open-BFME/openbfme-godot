extends RefCounted

## WP01-core-script-state - counters, script control and the counter-writing
## reads that feed them.
##
## PILOT PACKAGE. This file is the worked example for the other ~16 work
## packages: it is the shape every one of them should have. Read
## handlers/_registry.gd first for the rules; this file demonstrates them.
##
## Two things worth copying:
##
## 1. ARGUMENTS ARE READ POSITIONALLY, and this package is where that matters
##    most. Its signatures are inconsistent about where the counter goes:
##
##        SET_COUNTER_IN_SECONDS(COUNTER, REAL)                    counter first
##        SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER(PLAYER, COUNTER)  counter last
##        SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER(OBJECT_TYPE_LIST, PLAYER, COUNTER)
##        SET_COUNTER_TO_THREAT_FINDER_THREAT(COUNTER, THREAT_FINDER, THREAT_TYPE, PLAYER)
##
##    Searching the argument list for "the counter-typed one" happens to work
##    for the first and silently reads the wrong slot elsewhere. Every read
##    below is by INDEX, with the signature written above it.
##
## 2. A READ THAT COULD NOT BE ANSWERED IS NOT A ZERO. Every one of the
##    SET_..._TO_COUNTER actions asks the world a question. If the world cannot
##    answer, the handler returns WORLD_REFUSED and leaves the counter alone. It
##    does NOT write 0. A counter silently set to 0 is indistinguishable from a
##    player who genuinely has 0 command points, and a script gating on it would
##    take the wrong branch with no trace.

const PACKAGE := "WP01-core-script-state"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const World := preload("res://src/script/script_world.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# Counters set from a time in seconds.
	reg.action("SET_COUNTER_IN_SECONDS", _set_counter_in_seconds)
	reg.action("SET_RANDOM_COUNTER_IN_SECONDS", _set_random_counter_in_seconds)
	reg.condition("COUNTER_SECONDS", _condition_counter_seconds)

	# Counters set from a world read.
	reg.action("SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER", _command_points_available)
	reg.action("SET_PLAYER_COMMAND_POINTS_TOTAL_TO_COUNTER", _command_points_total)
	reg.action("SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER", _command_points_used)
	reg.action("SET_PLAYER_LIGHT_POINTS_TO_COUNTER", _light_points)
	reg.action("SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER", _ownership_of_type)
	reg.action("SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER_INCLUDE_DEAD", _ownership_of_type_with_dead)
	reg.action(
		"SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION",
		_objects_with_model_condition
	)
	reg.action("SET_COUNTER_TO_THREAT_FINDER_THREAT", _threat_finder_threat)

	# Script control.
	reg.action("CALL_SUBROUTINE", _call_subroutine)

	# Countdown timer display. Presentation, not simulation - see below.
	reg.action("ENABLE_COUNTDOWN_TIMER_DISPLAY", _enable_countdown_timer_display)
	reg.action("DISABLE_COUNTDOWN_TIMER_DISPLAY", _disable_countdown_timer_display)

	# NOT REGISTERED HERE, deliberately: SET_COUNTER_TO_CLIENT_RANDOM_VALUE.
	# The catalog lists it under WP01, but tranche 1
	# (script_handlers_core.gd) already registers it as a DELIBERATE refusal -
	# the reference warns it draws from the client-side random stream and
	# desyncs multiplayer, and this project runs lockstep. Registering it again
	# here would trip the registry's duplicate check, which is exactly what that
	# check is for. The overlap is a catalog bookkeeping artefact, not a gap.


# --- Counters from a time in seconds --------------------------------------
#
# UNIT NOTE. A counter is an integer, so "a counter holding seconds" has to hold
# a whole number of something. These handlers store INTERPRETER TICKS
# (SageScriptEnv.ticks_per_second), converted with the same rounding as the
# timer family, so SET_COUNTER_IN_SECONDS and COUNTER_SECONDS round-trip exactly
# and a seconds-counter agrees with a seconds-timer.
#
# The residual risk is a map that writes a counter with plain SET_COUNTER (a raw
# integer) and then reads it with COUNTER_SECONDS, or vice versa; those two
# would disagree by the tick rate. No retail map is known to do it, and there is
# no representation that satisfies both, so this is recorded as a known limit
# rather than papered over.


static func _set_counter_in_seconds(ctx: Dictionary) -> int:
	# SET_COUNTER_IN_SECONDS(COUNTER, REAL)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	env.set_counter(args.text(0), env.ticks_from_seconds(args.real(1)))
	return Dispatch.Status.OK


static func _set_random_counter_in_seconds(ctx: Dictionary) -> int:
	# SET_RANDOM_COUNTER_IN_SECONDS(COUNTER, REAL low, REAL high)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	var world: SageScriptWorld = ctx["world"]
	if not world.supports(World.CAP_RANDOM):
		ctx["detail"] = "world provides no deterministic random stream"
		return Dispatch.Status.WORLD_REFUSED
	var seconds := world.random_real(args.real(1), args.real(2))
	env.set_counter(args.text(0), env.ticks_from_seconds(seconds))
	return Dispatch.Status.OK


static func _condition_counter_seconds(ctx: Dictionary) -> int:
	# COUNTER_SECONDS(COUNTER, COMPARISON, REAL)
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	ctx["result"] = SageScriptParamTypes.compare_int(
		env.counter(args.text(0)),
		args.integer(1),
		env.ticks_from_seconds(args.real(2))
	)
	return Dispatch.Status.OK


# --- Counters from a world read -------------------------------------------


static func _store(ctx: Dictionary, counter_name: String, query: SageWorldQuery) -> int:
	## The shared tail of every SET_..._TO_COUNTER action: write the answer, or
	## refuse and leave the counter untouched. Never writes a fallback value.
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	(ctx["env"] as SageScriptEnv).set_counter(counter_name, query.as_int())
	return Dispatch.Status.OK


static func _command_points_available(ctx: Dictionary) -> int:
	# SET_PLAYER_COMMAND_POINTS_AVAILABLE_TO_COUNTER(PLAYER, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(1),
		(ctx["world"] as SageScriptWorld).players().command_points_available(args.text(0))
	)


static func _command_points_total(ctx: Dictionary) -> int:
	# SET_PLAYER_COMMAND_POINTS_TOTAL_TO_COUNTER(PLAYER, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(1),
		(ctx["world"] as SageScriptWorld).players().command_points_total(args.text(0))
	)


static func _command_points_used(ctx: Dictionary) -> int:
	# SET_PLAYER_COMMAND_POINTS_USED_TO_COUNTER(PLAYER, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(1),
		(ctx["world"] as SageScriptWorld).players().command_points_used(args.text(0))
	)


static func _light_points(ctx: Dictionary) -> int:
	# SET_PLAYER_LIGHT_POINTS_TO_COUNTER(PLAYER, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(1),
		(ctx["world"] as SageScriptWorld).players().light_points(args.text(0))
	)


static func _ownership_count(ctx: Dictionary, include_dead: bool) -> int:
	# SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER(OBJECT_TYPE_LIST, PLAYER, COUNTER)
	# The object-type LIST comes first and the player second - the one ordering
	# in this package a type-search would get most confidently wrong, since both
	# are strings.
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(2),
		(ctx["world"] as SageScriptWorld).players().object_count_of_types(
			args.text(1), args.text(0), include_dead
		)
	)


static func _ownership_of_type(ctx: Dictionary) -> int:
	return _ownership_count(ctx, false)


static func _ownership_of_type_with_dead(ctx: Dictionary) -> int:
	# SET_PLAYER_OWNERSHIP_OF_TYPE_COUNTER_INCLUDE_DEAD - same signature, and the
	# only difference is the flag, so it shares the body rather than being
	# copy-pasted with one word changed.
	return _ownership_count(ctx, true)


static func _objects_with_model_condition(ctx: Dictionary) -> int:
	# SET_COUNTER_TO_NUMBER_OBJECTS_PLAYER_OWNES_WITH_MODELCONDITION
	#   (PLAYER, MODEL_CONDITION, COUNTER)
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(2),
		(ctx["world"] as SageScriptWorld).players().object_count_with_model_condition(
			args.text(0), args.text(1)
		)
	)


static func _threat_finder_threat(ctx: Dictionary) -> int:
	# SET_COUNTER_TO_THREAT_FINDER_THREAT(COUNTER, THREAT_FINDER, THREAT_TYPE, PLAYER)
	# Counter FIRST here, and the player LAST - the opposite of the command-point
	# actions above. RotWK only.
	var args: SageScriptArgs = ctx["args"]
	return _store(
		ctx, args.text(0),
		(ctx["world"] as SageScriptWorld).players().threat_at(
			args.text(3), args.text(1), args.text(2)
		)
	)


# --- Script control -------------------------------------------------------


static func _call_subroutine(ctx: Dictionary) -> int:
	# CALL_SUBROUTINE(SCRIPT_SUBROUTINE)
	#
	# Runs another script body, which only the thing that OWNS script bodies can
	# do. SageScriptEnv exposes a `subroutine_runner` Callable for that owner to
	# install. Until something installs one this reports UNSUPPORTED and the
	# dispatcher records a gap naming the subroutine - it does not queue the call
	# for a consumer that may never exist, because an undrained queue is a silent
	# no-op with extra steps.
	var args: SageScriptArgs = ctx["args"]
	var env: SageScriptEnv = ctx["env"]
	if not env.has_subroutine_runner():
		ctx["detail"] = (
			"no subroutine runner is installed on the script environment, so "
			+ "subroutine '%s' cannot be executed"
		) % args.text(0)
		return Dispatch.Status.UNSUPPORTED
	if not env.call_subroutine(args.text(0)):
		ctx["detail"] = "subroutine '%s' was not found by the installed runner" % args.text(0)
		return Dispatch.Status.UNSUPPORTED
	return Dispatch.Status.OK


# --- Countdown timer display ----------------------------------------------
#
# These two carry no arguments and change nothing in the simulation: they show
# and hide the on-screen countdown readout. They therefore go to the UI
# PRESENTATION SINK rather than to a world facet, and a world with no UI adapter
# refuses them into a gap instead of pretending the HUD changed.
#
# They are grouped into WP01 by the catalog because they sit in the same
# WorldBuilder menu as the timers, not because they are script state.


static func _countdown_display(ctx: Dictionary, enabled: bool) -> int:
	var world: SageScriptWorld = ctx["world"]
	if not world.ui().emit(String(ctx["name"]), [enabled]):
		ctx["detail"] = "world has no UI presentation sink"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _enable_countdown_timer_display(ctx: Dictionary) -> int:
	# ENABLE_COUNTDOWN_TIMER_DISPLAY() - no arguments.
	return _countdown_display(ctx, true)


static func _disable_countdown_timer_display(ctx: Dictionary) -> int:
	# DISABLE_COUNTDOWN_TIMER_DISPLAY() - no arguments.
	return _countdown_display(ctx, false)
